/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Rubber Band Library
    An audio time-stretching and pitch-shifting library.
    Copyright 2007-2022 Particular Programs Ltd.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.

    Alternatively, if you have a valid commercial licence for the
    Rubber Band Library obtained by agreement with the copyright
    holders, you may redistribute and/or modify it under the terms
    described in that licence.

    If you wish to distribute code using the Rubber Band Library
    under terms other than those of the GNU General Public License,
    you must obtain a valid commercial licence before doing so.
*/

#ifndef RUBBERBAND_BIN_CLASSIFIER_H
#define RUBBERBAND_BIN_CLASSIFIER_H

#include "../common/Allocators.h"
#include "../common/MovingMedian.h"
#include "../common/RingBuffer.h"

#include <vector>
#include <memory>

namespace RubberBand {

class BinClassifier
{
public:
    enum class Classification {
        Harmonic = 0,
        Percussive = 1,
        Residual = 2,
        Silent = 3
    };

    struct Parameters {
        int binCount;
        int horizontalFilterLength;
        int horizontalFilterLag;
        int verticalFilterLength;
        double harmonicThreshold;
        double percussiveThreshold;
        double silenceThreshold;
        Parameters(int _binCount, int _horizontalFilterLength,
                   int _horizontalFilterLag, int _verticalFilterLength,
                   double _harmonicThreshold, double _percussiveThreshold,
                   double _silenceThreshold) :
            binCount(_binCount),
            horizontalFilterLength(_horizontalFilterLength),
            horizontalFilterLag(_horizontalFilterLag),
            verticalFilterLength(_verticalFilterLength),
            harmonicThreshold(_harmonicThreshold),
            percussiveThreshold(_percussiveThreshold),
            silenceThreshold(_silenceThreshold) { }
    };
    
    BinClassifier(Parameters parameters) :
        m_parameters(parameters),
        m_hFilters(new MovingMedianStack<double>(m_parameters.binCount,
                                                 m_parameters.horizontalFilterLength)),
        m_vFilter(new MovingMedian<double>(m_parameters.verticalFilterLength)),
        m_vfQueue(parameters.horizontalFilterLag)
    {
        int n = m_parameters.binCount;

        m_hf = allocate_and_zero<double>(n);
        m_vf = allocate_and_zero<double>(n);
        
        for (int i = 0; i < m_parameters.horizontalFilterLag; ++i) {
            double *entry = allocate_and_zero<double>(n);
            m_vfQueue.write(&entry, 1);
        }
    }

    ~BinClassifier()
    {
        while (m_vfQueue.getReadSpace() > 0) {
            double *entry = m_vfQueue.readOne();
            deallocate(entry);
        }

        deallocate(m_hf);
        deallocate(m_vf);
    }

    void reset()
    {
        m_hFilters->reset();
    }
    
    void classify(const double *const mag, // input, of at least binCount bins
                  Classification *classification) // output, of binCount bins
    {
        const int n = m_parameters.binCount;

        for (int i = 0; i < n; ++i) {
            m_hFilters->push(i, mag[i]);
            m_hf[i] = m_hFilters->get(i);
        }

        v_copy(m_vf, mag, n);
        MovingMedian<double>::filter(*m_vFilter, m_vf);

        if (m_parameters.horizontalFilterLag > 0) {
            double *lagged = m_vfQueue.readOne();
            m_vfQueue.write(&m_vf, 1);
            m_vf = lagged;
        }

        double eps = 1.0e-7;
        
        for (int i = 0; i < n; ++i) {
            Classification c;
            if (mag[i] < m_parameters.silenceThreshold) {
                c = Classification::Silent;
            } else if (double(m_hf[i]) / (double(m_vf[i]) + eps) >
                       m_parameters.harmonicThreshold) {
                c = Classification::Harmonic;
            } else if (double(m_vf[i]) / (double(m_hf[i]) + eps) >
                       m_parameters.percussiveThreshold) {
                c = Classification::Percussive;
            } else {
                c = Classification::Residual;
            }
            classification[i] = c;
        }
    }

protected:
    Parameters m_parameters;
    std::unique_ptr<MovingMedianStack<double>> m_hFilters;
    std::unique_ptr<MovingMedian<double>> m_vFilter;
    // We manage the queued frames through pointer swapping, hence
    // bare pointers here
    double *m_hf;
    double *m_vf;
    RingBuffer<double *> m_vfQueue;

    BinClassifier(const BinClassifier &) =delete;
    BinClassifier &operator=(const BinClassifier &) =delete;
};

}

#endif
