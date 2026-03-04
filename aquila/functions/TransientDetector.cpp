/**
 * @file TransientDetector.cpp
 *
 * High-frequency transient (glass break) detector.
 *
 * This file is part of the Aquila DSP library.
 * Aquila is free software, licensed under the MIT/X11 License. A copy of
 * the license is provided with the library in the LICENSE file.
 *
 * @package Aquila
 * @version 3.0.0-dev
 * @author Zbigniew Siciarz
 * @date 2007-2014
 * @license http://www.opensource.org/licenses/mit-license.php MIT
 * @since 3.0.0
 */

#include "TransientDetector.h"
#include "../transform/FftFactory.h"
#include "../source/SignalSource.h"
#include <algorithm>
#include <cmath>

namespace Aquila
{
    const FrequencyType TransientDetector::LOW_FREQUENCY  = 10000.0;
    const FrequencyType TransientDetector::HIGH_FREQUENCY = 15000.0;
    const double        TransientDetector::DETECTION_RATIO = 3.0;

    /**
     * Builds the detector and pre-allocates all working memory.
     *
     * The Hann window, the windowed-frame scratch space and the history
     * ring are all sized here and never touched again. The FFT bin range
     * for the 10-15 kHz band is resolved up front so the hot path is a
     * tight integer loop with no floating-point frequency maths.
     *
     * @param sampleFrequency sample frequency of incoming audio in Hz
     * @param fftSize STFT frame length in samples (power of 2)
     * @param historyLength number of past frames in the rolling average
     */
    TransientDetector::TransientDetector(FrequencyType sampleFrequency,
                                         std::size_t fftSize,
                                         std::size_t historyLength):
        m_sampleFrequency(sampleFrequency),
        m_fftSize(fftSize),
        m_lowBin(0),
        m_highBin(0),
        m_fft(FftFactory::getFft(fftSize)),
        m_window(fftSize, 0.0),
        m_frame(fftSize, 0.0),
        m_history(historyLength, 0.0),
        m_historyHead(0),
        m_historyFill(0),
        m_historySum(0.0),
        m_lastEnergy(0.0)
    {
        // Hann window: w[n] = 0.5 * (1 - cos(2*pi*n / (N-1)))
        for (std::size_t n = 0; n < m_fftSize; ++n)
        {
            m_window[n] = 0.5 * (1.0 -
                std::cos(2.0 * M_PI * n / static_cast<double>(m_fftSize - 1)));
        }

        // Map band edges to FFT bins. Real-input FFT has unique content
        // only up to N/2, so clamp the high edge there.
        const std::size_t nyquistBin = m_fftSize / 2;
        m_lowBin = static_cast<std::size_t>(
            LOW_FREQUENCY * m_fftSize / m_sampleFrequency + 0.5);
        m_highBin = static_cast<std::size_t>(
            HIGH_FREQUENCY * m_fftSize / m_sampleFrequency + 0.5);
        m_lowBin  = std::min(m_lowBin,  nyquistBin);
        m_highBin = std::min(m_highBin, nyquistBin);
    }

    /**
     * Runs the STFT over an audio buffer and checks for a transient.
     *
     * @param source audio buffer (at least fftSize samples)
     * @return true if any frame in the buffer exceeded the threshold
     */
    bool TransientDetector::process(const SignalSource& source)
    {
        const SampleType* data = source.toArray();
        const std::size_t length = source.getSamplesCount();

        bool detected = false;
        m_lastEnergy = 0.0;

        // Walk the buffer in non-overlapping STFT frames. Any partial
        // tail shorter than one frame is ignored rather than zero-padded
        // so we never touch memory we did not pre-allocate.
        std::size_t offset = 0;
        while (offset + m_fftSize <= length)
        {
            double energy = frameBandEnergy(data + offset);

            if (energy > m_lastEnergy)
            {
                m_lastEnergy = energy;
            }

            // Compare against the rolling background before this frame
            // joins it. Require at least one historical frame so the
            // very first buffer after power-up cannot fire.
            if (m_historyFill > 0)
            {
                double background = m_historySum /
                    static_cast<double>(m_historyFill);
                if (energy > DETECTION_RATIO * background)
                {
                    detected = true;
                }
            }

            pushHistory(energy);
            offset += m_fftSize;
        }

        return detected;
    }

    /**
     * Returns the current rolling background energy.
     *
     * @return mean of the history ring buffer, or 0 if still empty
     */
    double TransientDetector::getBackgroundEnergy() const
    {
        if (0 == m_historyFill)
        {
            return 0.0;
        }
        return m_historySum / static_cast<double>(m_historyFill);
    }

    /**
     * Clears the rolling background history.
     *
     * Does not reallocate; the ring buffer storage is reused.
     */
    void TransientDetector::reset()
    {
        m_historyHead = 0;
        m_historyFill = 0;
        m_historySum  = 0.0;
        m_lastEnergy  = 0.0;
    }

    /**
     * Windows one frame, transforms it, and returns band energy.
     *
     * @param samples pointer to fftSize contiguous input samples
     * @return sum of |X[k]|^2 for k in [m_lowBin, m_highBin]
     */
    double TransientDetector::frameBandEnergy(const SampleType* samples)
    {
        // Apply the pre-computed Hann window into the scratch frame.
        for (std::size_t n = 0; n < m_fftSize; ++n)
        {
            m_frame[n] = samples[n] * m_window[n];
        }

        // Forward transform. The FFT engine was planned in the
        // constructor; this call only runs the butterfly.
        SpectrumType spectrum = m_fft->fft(&m_frame[0]);

        // Integrate squared magnitude across the target bins. Using
        // real()*real() + imag()*imag() avoids the sqrt hidden inside
        // std::abs(ComplexType) - we want energy, not amplitude.
        double energy = 0.0;
        for (std::size_t k = m_lowBin; k <= m_highBin; ++k)
        {
            const ComplexType& c = spectrum[k];
            energy += c.real() * c.real() + c.imag() * c.imag();
        }

        return energy;
    }

    /**
     * Pushes an energy value into the ring buffer in O(1).
     *
     * Keeps a running sum so getBackgroundEnergy() never has to loop.
     * When the ring wraps, the value being overwritten is subtracted
     * from the sum before the new value is added.
     *
     * @param energy band energy of the frame just processed
     */
    void TransientDetector::pushHistory(double energy)
    {
        const std::size_t capacity = m_history.size();

        if (m_historyFill < capacity)
        {
            m_history[m_historyHead] = energy;
            m_historySum += energy;
            ++m_historyFill;
        }
        else
        {
            m_historySum -= m_history[m_historyHead];
            m_history[m_historyHead] = energy;
            m_historySum += energy;
        }

        ++m_historyHead;
        if (m_historyHead >= capacity)
        {
            m_historyHead = 0;
        }
    }
}
