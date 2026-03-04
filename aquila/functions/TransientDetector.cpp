/**
 * @file TransientDetector.cpp
 *
 * Implementation of the STFT-based glass-break transient detector.
 *
 * This file is part of the Aquila DSP library.
 * Aquila is free software, licensed under the MIT/X11 License. A copy of
 * the license is provided with the library in the LICENSE file.
 *
 * @package Aquila
 * @version 3.0.0-dev
 * @author Aquila contributors
 * @date 2007-2014
 * @license http://www.opensource.org/licenses/mit-license.php MIT
 * @since 3.0.0
 */

#include "TransientDetector.h"
#include "../transform/FftFactory.h"
#include "../source/SignalSource.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Aquila
{
    /**
     * Constructs the detector and pre-allocates every internal buffer.
     *
     * The Hann window, windowed-sample scratch buffer, and circular
     * background-history buffer are all sized here so that processFrame()
     * never touches the heap.
     */
    TransientDetector::TransientDetector(std::size_t fftSize,
                                         FrequencyType sampleRate,
                                         std::size_t backgroundFrames,
                                         double threshold):
        m_fftSize(fftSize),
        m_sampleRate(sampleRate),
        m_backgroundFrames(backgroundFrames),
        m_threshold(threshold),
        m_fft(FftFactory::getFft(fftSize)),
        m_window(fftSize),
        m_windowedBuffer(fftSize),
        m_bandStart(0),
        m_bandEnd(0),
        m_backgroundHistory(backgroundFrames, 0.0),
        m_historyIndex(0),
        m_historyCount(0),
        m_backgroundSum(0.0),
        m_currentRatio(0.0)
    {
        // Pre-compute Hann window coefficients.
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            m_window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i /
                          static_cast<double>(fftSize - 1)));
        }

        // Map the 10-15 kHz band to FFT bin indices.
        // Bin k corresponds to frequency k * sampleRate / fftSize.
        double binWidth = sampleRate / static_cast<double>(fftSize);
        m_bandStart = static_cast<std::size_t>(std::ceil(10000.0 / binWidth));
        m_bandEnd   = static_cast<std::size_t>(std::floor(15000.0 / binWidth)) + 1;

        // Clamp to the valid range (bins above N/2 are mirrored).
        std::size_t maxBin = fftSize / 2;
        if (m_bandStart > maxBin)
        {
            m_bandStart = maxBin;
        }
        if (m_bandEnd > maxBin)
        {
            m_bandEnd = maxBin;
        }
    }

    /**
     * Processes a single audio frame through the STFT detection pipeline.
     *
     * 1. Apply the pre-computed Hann window (writes into m_windowedBuffer).
     * 2. Forward-FFT via the factory-provided engine.
     * 3. Sum |X[k]|^2 across the 10-15 kHz bins.
     * 4. Update the rolling background average (circular buffer).
     * 5. Compare current energy to background; flag if >= threshold.
     *
     * @param frame  SignalSource containing at least m_fftSize samples
     * @return true when band energy >= threshold * background average
     */
    bool TransientDetector::processFrame(const SignalSource& frame)
    {
        // --- 1. Windowing (zero-allocation: writes into pre-sized buffer) ---
        const SampleType* samples = frame.toArray();
        for (std::size_t i = 0; i < m_fftSize; ++i)
        {
            m_windowedBuffer[i] = samples[i] * m_window[i];
        }

        // --- 2. Forward FFT via the shared engine ---
        SpectrumType spectrum = m_fft->fft(m_windowedBuffer.data());

        // --- 3. Band energy in 10-15 kHz ---
        double energy = computeBandEnergy(spectrum);

        // --- 4. Rolling background average (circular buffer) ---
        if (m_historyCount < m_backgroundFrames)
        {
            // Still filling the history buffer.
            m_backgroundHistory[m_historyIndex] = energy;
            m_backgroundSum += energy;
            m_historyCount++;
        }
        else
        {
            // Overwrite the oldest entry.
            m_backgroundSum -= m_backgroundHistory[m_historyIndex];
            m_backgroundHistory[m_historyIndex] = energy;
            m_backgroundSum += energy;
        }
        m_historyIndex = (m_historyIndex + 1) % m_backgroundFrames;

        // --- 5. Detection decision ---
        double avg = getBackgroundAverage();
        m_currentRatio = (avg > 0.0) ? (energy / avg) : 0.0;

        return m_currentRatio >= m_threshold;
    }

    /**
     * Returns the mean band energy over the filled portion of the
     * circular history buffer.
     */
    double TransientDetector::getBackgroundAverage() const
    {
        if (m_historyCount == 0)
        {
            return 0.0;
        }
        return m_backgroundSum / static_cast<double>(m_historyCount);
    }

    /**
     * Clears background history and resets the detector to its
     * initial state (as if no frames had been processed).
     */
    void TransientDetector::reset()
    {
        std::fill(m_backgroundHistory.begin(),
                  m_backgroundHistory.end(), 0.0);
        m_historyIndex = 0;
        m_historyCount = 0;
        m_backgroundSum = 0.0;
        m_currentRatio = 0.0;
    }

    /**
     * Accumulates squared magnitudes of the spectrum bins that fall
     * inside the target frequency band [m_bandStart, m_bandEnd).
     */
    double TransientDetector::computeBandEnergy(
        const SpectrumType& spectrum) const
    {
        double energy = 0.0;
        for (std::size_t k = m_bandStart; k < m_bandEnd; ++k)
        {
            double mag = std::abs(spectrum[k]);
            energy += mag * mag;
        }
        return energy;
    }
}
