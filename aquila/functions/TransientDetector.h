/**
 * @file TransientDetector.h
 *
 * STFT-based transient detector targeting the 10-15 kHz band
 * (glass-breaking signature). Compares per-frame band energy against
 * a rolling background average; anything past 300 % is a detection.
 *
 * Designed for embedded hardware: every buffer is pre-allocated at
 * construction time. The processing path performs zero heap allocations
 * (the FFT engine's own internal allocations are outside our control).
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

#ifndef TRANSIENTDETECTOR_H
#define TRANSIENTDETECTOR_H

#include "../global.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace Aquila
{
    class Fft;
    class SignalSource;

    /**
     * Detects glass-breaking transients by monitoring energy spikes
     * in the 10-15 kHz frequency band via short-time Fourier transform.
     *
     * Usage:
     * @code
     *   TransientDetector detector(1024, 44100.0);
     *   // feed frames continuously
     *   if (detector.processFrame(audioFrame)) {
     *       // glass break detected
     *   }
     * @endcode
     */
    class AQUILA_EXPORT TransientDetector
    {
    public:
        /**
         * Creates a detector with the given FFT parameters.
         *
         * @param fftSize       FFT length in samples (must be power of 2)
         * @param sampleRate    audio sample rate in Hz
         * @param backgroundFrames  rolling-average window length (default 20)
         * @param threshold     detection threshold as a ratio (3.0 = 300 %)
         */
        TransientDetector(std::size_t fftSize,
                          FrequencyType sampleRate,
                          std::size_t backgroundFrames = 20,
                          double threshold = 3.0);

        /**
         * Processes one frame of audio data.
         *
         * @param frame  audio frame wrapped as a SignalSource
         *               (must contain at least fftSize samples)
         * @return true if a glass-breaking transient was detected
         */
        bool processFrame(const SignalSource& frame);

        /**
         * Returns the energy-ratio from the most recent processFrame() call
         * (current band energy / rolling background average).
         */
        double getCurrentRatio() const { return m_currentRatio; }

        /**
         * Returns the current rolling background average of band energy.
         */
        double getBackgroundAverage() const;

        /**
         * Resets all detector state (background history, counters, ratio).
         */
        void reset();

    private:
        std::size_t m_fftSize;
        FrequencyType m_sampleRate;
        std::size_t m_backgroundFrames;
        double m_threshold;

        /// FFT engine obtained through FftFactory.
        std::shared_ptr<Fft> m_fft;

        /// Pre-computed Hann window coefficients (length = fftSize).
        std::vector<double> m_window;

        /// Windowed sample buffer written into each processFrame() call.
        std::vector<SampleType> m_windowedBuffer;

        /// First FFT bin inside the 10 kHz lower edge (inclusive).
        std::size_t m_bandStart;

        /// One past the last FFT bin inside the 15 kHz upper edge (exclusive).
        std::size_t m_bandEnd;

        /// Circular buffer of past band energies (length = backgroundFrames).
        std::vector<double> m_backgroundHistory;
        std::size_t m_historyIndex;
        std::size_t m_historyCount;
        double m_backgroundSum;

        /// Energy-to-background ratio from the latest frame.
        double m_currentRatio;

        /**
         * Sums |X[k]|^2 over the target frequency band.
         */
        double computeBandEnergy(const SpectrumType& spectrum) const;
    };
}

#endif // TRANSIENTDETECTOR_H
