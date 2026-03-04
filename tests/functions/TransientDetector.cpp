#include "aquila/global.h"
#include "aquila/functions/TransientDetector.h"
#include "aquila/source/SignalSource.h"
#include "aquila/source/generator/SineGenerator.h"
#include "UnitTest++/UnitTest++.h"
#include <cmath>
#include <vector>


SUITE(TransientDetector)
{
    // 44.1 kHz puts the 10-15 kHz band comfortably below Nyquist and
    // is the most likely sample rate for the target hardware.
    const Aquila::FrequencyType sampleFrequency = 44100.0;
    const std::size_t FFT_SIZE = 512;

    // Fills a buffer with a tone at the given frequency.
    void fillTone(std::vector<Aquila::SampleType>& buf,
                  Aquila::FrequencyType freq, double amplitude)
    {
        for (std::size_t n = 0; n < buf.size(); ++n)
        {
            buf[n] = amplitude *
                std::sin(2.0 * M_PI * freq * n / sampleFrequency);
        }
    }

    TEST(BandEdges)
    {
        CHECK_EQUAL(10000.0, Aquila::TransientDetector::LOW_FREQUENCY);
        CHECK_EQUAL(15000.0, Aquila::TransientDetector::HIGH_FREQUENCY);
        CHECK_EQUAL(3.0, Aquila::TransientDetector::DETECTION_RATIO);
    }

    TEST(NoDetectionOnFirstBuffer)
    {
        // With an empty history there is no background to compare
        // against, so the first buffer must never report a transient
        // regardless of its content.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        Aquila::SineGenerator gen(sampleFrequency);
        gen.setFrequency(12500.0).setAmplitude(1.0).generate(FFT_SIZE);

        CHECK_EQUAL(false, detector.process(gen));
    }

    TEST(NoDetectionOnStationaryNoise)
    {
        // A constant-energy signal should settle into the background
        // and never cross the 300% threshold.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> buf(FFT_SIZE);
        fillTone(buf, 12500.0, 0.1);
        Aquila::SignalSource source(buf, sampleFrequency);

        for (int i = 0; i < 16; ++i)
        {
            CHECK_EQUAL(false, detector.process(source));
        }
    }

    TEST(DetectsInBandSpike)
    {
        // Establish a quiet background in-band, then hit it with a
        // much louder tone at the same frequency. The band energy
        // scales with amplitude squared, so a 10x amplitude jump is
        // a 100x energy jump - far past 300%.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> quiet(FFT_SIZE);
        fillTone(quiet, 12500.0, 0.05);
        Aquila::SignalSource quietSrc(quiet, sampleFrequency);

        for (int i = 0; i < 8; ++i)
        {
            detector.process(quietSrc);
        }

        double background = detector.getBackgroundEnergy();
        CHECK(background > 0.0);

        std::vector<Aquila::SampleType> spike(FFT_SIZE);
        fillTone(spike, 12500.0, 0.5);
        Aquila::SignalSource spikeSrc(spike, sampleFrequency);

        CHECK_EQUAL(true, detector.process(spikeSrc));
        CHECK(detector.getBandEnergy() >
            Aquila::TransientDetector::DETECTION_RATIO * background);
    }

    TEST(IgnoresOutOfBandSpike)
    {
        // A loud spike well below 10 kHz must leave the band energy
        // effectively untouched. Prime the background with a small
        // in-band component so the background is non-zero.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> quiet(FFT_SIZE);
        fillTone(quiet, 12500.0, 0.05);
        Aquila::SignalSource quietSrc(quiet, sampleFrequency);

        for (int i = 0; i < 8; ++i)
        {
            detector.process(quietSrc);
        }

        // 1 kHz spike - same amplitude jump as the positive test.
        std::vector<Aquila::SampleType> spike(FFT_SIZE);
        fillTone(spike, 1000.0, 0.5);
        Aquila::SignalSource spikeSrc(spike, sampleFrequency);

        CHECK_EQUAL(false, detector.process(spikeSrc));
    }

    TEST(RatioJustBelowThreshold)
    {
        // Energy ~ amplitude^2. To land just under 3x energy we need
        // an amplitude ratio of sqrt(2.9) ~= 1.70. Verify this does
        // not fire.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> quiet(FFT_SIZE);
        fillTone(quiet, 12500.0, 0.1);
        Aquila::SignalSource quietSrc(quiet, sampleFrequency);

        for (int i = 0; i < 8; ++i)
        {
            detector.process(quietSrc);
        }

        std::vector<Aquila::SampleType> bump(FFT_SIZE);
        fillTone(bump, 12500.0, 0.1 * std::sqrt(2.9));
        Aquila::SignalSource bumpSrc(bump, sampleFrequency);

        CHECK_EQUAL(false, detector.process(bumpSrc));
    }

    TEST(RatioJustAboveThreshold)
    {
        // Amplitude ratio of sqrt(3.1) ~= 1.76 should fire.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> quiet(FFT_SIZE);
        fillTone(quiet, 12500.0, 0.1);
        Aquila::SignalSource quietSrc(quiet, sampleFrequency);

        for (int i = 0; i < 8; ++i)
        {
            detector.process(quietSrc);
        }

        std::vector<Aquila::SampleType> bump(FFT_SIZE);
        fillTone(bump, 12500.0, 0.1 * std::sqrt(3.1));
        Aquila::SignalSource bumpSrc(bump, sampleFrequency);

        CHECK_EQUAL(true, detector.process(bumpSrc));
    }

    TEST(MultiFrameBuffer)
    {
        // A buffer longer than one FFT frame is walked in blocks.
        // Put the spike in the second half to prove the whole buffer
        // is scanned.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> quiet(FFT_SIZE);
        fillTone(quiet, 12500.0, 0.05);
        Aquila::SignalSource quietSrc(quiet, sampleFrequency);

        for (int i = 0; i < 8; ++i)
        {
            detector.process(quietSrc);
        }

        // Two frames worth: first quiet, second loud.
        std::vector<Aquila::SampleType> big(2 * FFT_SIZE);
        for (std::size_t n = 0; n < FFT_SIZE; ++n)
        {
            double t = static_cast<double>(n) / sampleFrequency;
            big[n]            = 0.05 * std::sin(2.0 * M_PI * 12500.0 * t);
            big[n + FFT_SIZE] = 0.50 * std::sin(2.0 * M_PI * 12500.0 * t);
        }
        Aquila::SignalSource bigSrc(big, sampleFrequency);

        CHECK_EQUAL(true, detector.process(bigSrc));
    }

    TEST(ShortBufferIgnored)
    {
        // Buffers shorter than one FFT frame contribute nothing.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> tiny(FFT_SIZE / 2, 0.5);
        Aquila::SignalSource tinySrc(tiny, sampleFrequency);

        CHECK_EQUAL(false, detector.process(tinySrc));
        CHECK_EQUAL(0.0, detector.getBandEnergy());
        CHECK_EQUAL(0.0, detector.getBackgroundEnergy());
    }

    TEST(RollingAverageWraps)
    {
        // With a history of 4, the fifth frame must evict the first.
        // Feed four equal-energy frames, read the background, feed a
        // fifth equal frame, background must not change.
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 4);

        std::vector<Aquila::SampleType> buf(FFT_SIZE);
        fillTone(buf, 12500.0, 0.1);
        Aquila::SignalSource source(buf, sampleFrequency);

        for (int i = 0; i < 4; ++i)
        {
            detector.process(source);
        }
        double bgFull = detector.getBackgroundEnergy();
        CHECK(bgFull > 0.0);

        detector.process(source);
        CHECK_CLOSE(bgFull, detector.getBackgroundEnergy(), bgFull * 1e-9);
    }

    TEST(ResetClearsHistory)
    {
        Aquila::TransientDetector detector(sampleFrequency, FFT_SIZE, 8);

        std::vector<Aquila::SampleType> buf(FFT_SIZE);
        fillTone(buf, 12500.0, 0.1);
        Aquila::SignalSource source(buf, sampleFrequency);

        detector.process(source);
        CHECK(detector.getBackgroundEnergy() > 0.0);

        detector.reset();
        CHECK_EQUAL(0.0, detector.getBackgroundEnergy());
        CHECK_EQUAL(0.0, detector.getBandEnergy());

        // After reset the first-buffer guard is back in effect.
        CHECK_EQUAL(false, detector.process(source));
    }
}
