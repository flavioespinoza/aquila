/**
 * @file TransientDetector.h
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
     * Detects broadband high-frequency transients via STFT band energy.
     *
     * Glass breaking produces a sharp energy spike in the 10-15 kHz band.
     * Each incoming buffer is split into fixed-size frames, windowed, and
     * transformed with the shared FFT engine. The energy contained in the
     * target band is compared against a rolling average of past frame
     * energies; a frame exceeding the average by DETECTION_RATIO is
     * reported as a transient.
     *
     * Every buffer used on the processing path is sized exactly once in
     * the constructor. process() performs no heap allocation of its own;
     * the one unavoidable allocation is the SpectrumType returned by the
     * library FFT interface, which is a contract we inherit unchanged.
     */
    class AQUILA_EXPORT TransientDetector
    {
    public:
        /**
         * Builds the detector and pre-allocates all working memory.
         *
         * The FFT engine is obtained once from FftFactory so its internal
         * plan / twiddle tables are computed here rather than at run time.
         *
         * @param sampleFrequency sample frequency of incoming audio in Hz
         * @param fftSize STFT frame length in samples (power of 2)
         * @param historyLength number of past frames in the rolling average
         */
        TransientDetector(FrequencyType sampleFrequency,
                          std::size_t fftSize = 512,
                          std::size_t historyLength = 32);

        /**
         * Runs the STFT over an audio buffer and checks for a transient.
         *
         * The buffer is walked in non-overlapping frames of fftSize
         * samples. For every complete frame the 10-15 kHz band energy is
         * extracted and compared against the current rolling background.
         * The frame energy is pushed into the background history after
         * the comparison so a spike does not raise its own reference.
         *
         * @param source audio buffer (at least fftSize samples)
         * @return true if any frame in the buffer exceeded the threshold
         */
        bool process(const SignalSource& source);

        /**
         * Returns the peak band energy seen in the most recent buffer.
         *
         * @return energy value (sum of squared magnitudes in the band)
         */
        double getBandEnergy() const
        {
            return m_lastEnergy;
        }

        /**
         * Returns the current rolling background energy.
         *
         * @return mean of the history ring buffer, or 0 if still empty
         */
        double getBackgroundEnergy() const;

        /**
         * Clears the rolling background history.
         */
        void reset();

        /**
         * Lower edge of the detection band in Hz.
         */
        static const FrequencyType LOW_FREQUENCY;

        /**
         * Upper edge of the detection band in Hz.
         */
        static const FrequencyType HIGH_FREQUENCY;

        /**
         * Energy ratio above background that counts as a detection.
         */
        static const double DETECTION_RATIO;

    private:
        TransientDetector(const TransientDetector&);
        const TransientDetector& operator=(const TransientDetector&);

        /**
         * Windows one frame, transforms it, and returns band energy.
         *
         * @param samples pointer to fftSize contiguous input samples
         * @return sum of |X[k]|^2 for k in [m_lowBin, m_highBin]
         */
        double frameBandEnergy(const SampleType* samples);

        /**
         * Pushes an energy value into the ring buffer in O(1).
         *
         * @param energy band energy of the frame just processed
         */
        void pushHistory(double energy);

        /**
         * Sample frequency of the audio stream.
         */
        FrequencyType m_sampleFrequency;

        /**
         * STFT frame length.
         */
        std::size_t m_fftSize;

        /**
         * First FFT bin inside the detection band (inclusive).
         */
        std::size_t m_lowBin;

        /**
         * Last FFT bin inside the detection band (inclusive).
         */
        std::size_t m_highBin;

        /**
         * A shared pointer to FFT algorithm class.
         */
        std::shared_ptr<Fft> m_fft;

        /**
         * Pre-computed Hann window coefficients (length fftSize).
         */
        std::vector<SampleType> m_window;

        /**
         * Scratch space for the windowed frame (length fftSize).
         */
        std::vector<SampleType> m_frame;

        /**
         * Ring buffer of past frame energies.
         */
        std::vector<double> m_history;

        /**
         * Write position in the history ring.
         */
        std::size_t m_historyHead;

        /**
         * How many valid entries the ring currently holds.
         */
        std::size_t m_historyFill;

        /**
         * Running sum of m_history for O(1) average lookup.
         */
        double m_historySum;

        /**
         * Peak band energy observed in the last call to process().
         */
        double m_lastEnergy;
    };
}

#endif // TRANSIENTDETECTOR_H
