/**
 * @file tests/functions/TransientDetector.cpp
 *
 * Unit tests for the STFT-based glass-break transient detector.
 */

#include "aquila/global.h"
#include "aquila/source/SignalSource.h"
#include "aquila/functions/TransientDetector.h"
#include "UnitTest++/UnitTest++.h"
#include <cmath>
#include <cstddef>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Helper: build a single-frame SignalSource containing a pure sine tone.
static Aquila::SignalSource makeSineFrame(
    std::size_t fftSize,
    Aquila::FrequencyType sampleRate,
    Aquila::FrequencyType freq,
    double amplitude)
{
    std::vector<Aquila::SampleType> samples(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
    {
        samples[i] = amplitude *
            std::sin(2.0 * M_PI * freq * i / sampleRate);
    }
    return Aquila::SignalSource(std::move(samples), sampleRate);
}

/// Helper: build a silent frame (all zeros).
static Aquila::SignalSource makeSilentFrame(
    std::size_t fftSize,
    Aquila::FrequencyType sampleRate)
{
    std::vector<Aquila::SampleType> samples(fftSize, 0.0);
    return Aquila::SignalSource(std::move(samples), sampleRate);
}

SUITE(TransientDetector)
{
    const std::size_t FFT_SIZE = 1024;
    const Aquila::FrequencyType SAMPLE_RATE = 44100.0;
    const std::size_t BG_FRAMES = 10;

    // Silence should never trigger a detection.
    TEST(NoDetectionOnSilence)
    {
        Aquila::TransientDetector detector(FFT_SIZE, SAMPLE_RATE, BG_FRAMES);

        for (int i = 0; i < 20; ++i)
        {
            bool detected = detector.processFrame(
                makeSilentFrame(FFT_SIZE, SAMPLE_RATE));
            CHECK(!detected);
        }
    }

    // A steady low-frequency tone has no energy in the 10-15 kHz band.
    TEST(NoDetectionOnLowFrequency)
    {
        Aquila::TransientDetector detector(FFT_SIZE, SAMPLE_RATE, BG_FRAMES);

        Aquila::SignalSource frame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 200.0, 1.0);

        for (int i = 0; i < 20; ++i)
        {
            bool detected = detector.processFrame(frame);
            CHECK(!detected);
        }
    }

    // After a quiet 12 kHz background, a loud 12 kHz burst must trigger.
    TEST(DetectsHighFrequencySpike)
    {
        Aquila::TransientDetector detector(FFT_SIZE, SAMPLE_RATE, BG_FRAMES);

        // Build background with very quiet energy in the target band.
        Aquila::SignalSource quietFrame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 12000.0, 0.01);
        for (std::size_t i = 0; i < BG_FRAMES; ++i)
        {
            detector.processFrame(quietFrame);
        }

        // Loud burst in the same band.
        Aquila::SignalSource loudFrame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 12000.0, 1.0);
        bool detected = detector.processFrame(loudFrame);
        CHECK(detected);
        CHECK(detector.getCurrentRatio() >= 3.0);
    }

    // 10x amplitude jump → 100x energy jump → ratio well above 3.
    TEST(RatioReflectsEnergyDifference)
    {
        Aquila::TransientDetector detector(FFT_SIZE, SAMPLE_RATE, BG_FRAMES);

        Aquila::SignalSource quietFrame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 12000.0, 0.1);
        for (std::size_t i = 0; i < BG_FRAMES; ++i)
        {
            detector.processFrame(quietFrame);
        }

        Aquila::SignalSource loudFrame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 12000.0, 1.0);
        detector.processFrame(loudFrame);
        CHECK(detector.getCurrentRatio() > 3.0);
    }

    // A constant-amplitude signal at 12 kHz should settle into a
    // steady background with no spurious detections once filled.
    TEST(SteadySignalNoFalsePositive)
    {
        Aquila::TransientDetector detector(FFT_SIZE, SAMPLE_RATE, BG_FRAMES);

        Aquila::SignalSource frame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 12000.0, 0.5);

        for (int i = 0; i < 30; ++i)
        {
            bool detected = detector.processFrame(frame);
            // After the background fills, every frame looks like
            // the average, so the ratio should be ~1.0.
            if (i >= static_cast<int>(BG_FRAMES))
            {
                CHECK(!detected);
            }
        }
    }

    // reset() must clear all state.
    TEST(ResetClearsState)
    {
        Aquila::TransientDetector detector(FFT_SIZE, SAMPLE_RATE, BG_FRAMES);

        Aquila::SignalSource frame =
            makeSineFrame(FFT_SIZE, SAMPLE_RATE, 12000.0, 1.0);
        detector.processFrame(frame);

        detector.reset();
        CHECK_CLOSE(0.0, detector.getCurrentRatio(), 0.000001);
        CHECK_CLOSE(0.0, detector.getBackgroundAverage(), 0.000001);
    }

    // When the sample rate is so low that 10-15 kHz is above Nyquist,
    // band energy is always zero and no detection should fire.
    TEST(NoDetectionWhenBandAboveNyquist)
    {
        Aquila::FrequencyType lowRate = 16000.0;
        Aquila::TransientDetector detector(FFT_SIZE, lowRate, BG_FRAMES);

        // 7 kHz tone (below Nyquist for 16 kHz rate).
        Aquila::SignalSource frame =
            makeSineFrame(FFT_SIZE, lowRate, 7000.0, 1.0);

        for (int i = 0; i < 20; ++i)
        {
            bool detected = detector.processFrame(frame);
            CHECK(!detected);
        }
    }
}
