/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009-2011, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#ifndef __PHOTOMONTAGE_HPP__
#define __PHOTOMONTAGE_HPP__

#include <opencv2/core.hpp>

#include "norm2.hpp"
#include "algo.hpp"
#include "annf.hpp"
#include "gcgraph.hpp"

#define GCInfinity 10*1000*1000*1000.0
#define EFFECTIVE_HEIGHT 600
#define EFFECTIVE_WIDTH  800



template <typename Tp> class Photomontage
{
private:
    const std::vector <cv::Mat> &images; // vector of images for different labels
    const std::vector <cv::Mat>  &masks; // vector of definition domains for each image

    std::vector <cv::Mat> labelings; // vector of labelings for different expansions
    std::vector <double>  distances; // vector of max-flow costs for different labeling

    const int height;
    const int width;
    const int type;
    const int channels;
    const int lsize;

    bool multiscale; // if true, Photomontage use coarse-to-fine scheme

    double singleExpansion(const cv::Mat &x_i, const int alpha); // single neighbor computing
    void gradientDescent(const cv::Mat &x_0, cv::Mat x_n); // gradient descent in alpha-expansion topology

protected:
    virtual double dist(const Tp &l1p1, const Tp &l1p2, const Tp &l2p1, const Tp &l2p2);
    virtual void setWeights(GCGraph <double> &graph, const cv::Point &pA, const cv::Point &pB, const int lA, const int lB, const int lX);

public:
    Photomontage(const std::vector <cv::Mat> &images, const std::vector <cv::Mat> &masks, bool multiscale = true);
    virtual ~Photomontage(){};

    void assignLabeling(cv::Mat &img);
    void assignResImage(cv::Mat &img);
};

template <typename Tp> double Photomontage <Tp>::
dist(const Tp &l1p1, const Tp &l1p2, const Tp &l2p1, const Tp &l2p2)
{
    return norm2(l1p1, l2p1) + norm2(l1p2, l2p2);
}

template <typename Tp> void Photomontage <Tp>::
setWeights(GCGraph <double> &graph, const cv::Point &pA, const cv::Point &pB, const int lA, const int lB, const int lX)
{
    if (lA == lB)
    {
        /** Link from A to B **/
        double weightAB = dist( images[lA].at<Tp>(pA),
                                images[lA].at<Tp>(pB),
                                images[lX].at<Tp>(pA),
                                images[lX].at<Tp>(pB) );
        graph.addEdges(pA.y*width + pA.x, pB.y*width + pB.x, weightAB, weightAB);
    }
    else
    {
        int X = graph.addVtx();

        /** Link from X to sink **/
        double weightXS = dist( images[lA].at<Tp>(pA),
                                images[lA].at<Tp>(pB),
                                images[lB].at<Tp>(pA),
                                images[lB].at<Tp>(pB) );
        graph.addTermWeights(X, 0, weightXS);

        /** Link from A to X **/
        double weightAX = dist( images[lA].at<Tp>(pA),
                                images[lA].at<Tp>(pB),
                                images[lX].at<Tp>(pA),
                                images[lX].at<Tp>(pB) );
        graph.addEdges(pA.y*width + pA.x, X, weightAX, weightAX);

        /** Link from X to B **/
        double weightXB = dist( images[lX].at<Tp>(pA),
                                images[lX].at<Tp>(pB),
                                images[lB].at<Tp>(pA),
                                images[lB].at<Tp>(pB) );
        graph.addEdges(X, pB.y*width + pB.x, weightXB, weightXB);
    }
}

template <typename Tp> double Photomontage <Tp>::
singleExpansion(const cv::Mat &x_i, const int alpha)
{
    int actualEdges = (height - 1)*width + height*(width - 1);
    GCGraph <double> graph(actualEdges + height*width, 2*actualEdges);

    /** Terminal links **/
    for (int i = 0; i < height; ++i)
    {
        const uchar *maskAlphaRow = masks[alpha].ptr <uchar>(i);
        const int *labelRow = (const int *) x_i.ptr <int>(i);

        for (int j = 0; j < width; ++j)
            graph.addTermWeights( graph.addVtx(),
                                  maskAlphaRow[j] ? 0 : GCInfinity,
             masks[ labelRow[j] ].at<uchar>(i, j) ? 0 : GCInfinity );
    }

    /** Neighbor links **/
    for (int i = 0; i < height - 1; ++i)
    {
        const int *currentRow = (const int *) x_i.ptr <int>(i);
        const int *nextRow = (const int *) x_i.ptr <int>(i + 1);

        for (int j = 0; j < width - 1; ++j)
        {
            setWeights( graph, cv::Point(i, j), cv::Point(i, j + 1), currentRow[j], currentRow[j + 1], alpha );
            setWeights( graph, cv::Point(i, j), cv::Point(i + 1, j), currentRow[j],     nextRow[j],    alpha );
        }

        setWeights( graph, cv::Point(i, width - 1), cv::Point(i + 1, width - 1),
                    currentRow[width - 1], nextRow[width - 1], alpha );
    }

    const int *currentRow = (const int *) x_i.ptr <int>(height - 1);
    for (int i = 0; i < width - 1; ++i)
        setWeights( graph, cv::Point(height - 1, i), cv::Point(height - 1, i + 1),
                    currentRow[i], currentRow[i + 1], alpha );

    /** Max-flow computation **/
    double result = graph.maxFlow();

    /** Writing results **/
    labelings[alpha].create( height, width, CV_32SC1 );
    for (int i = 0; i < height; ++i)
    {
        const int *inRow = (const int *) x_i.ptr <int>(i);
        int *outRow = (int *) labelings[alpha].ptr <int>(i);

        for (int j = 0; j < width; ++j)
            outRow[j] = graph.inSourceSegment(i*width + j) ? inRow[j] : alpha;
    }

    return result;
}

template <typename Tp> void Photomontage <Tp>::
gradientDescent(const cv::Mat &x_0, cv::Mat x_n)
{
    double optValue = std::numeric_limits<double>::max();
    x_0.copyTo(x_n);

    for (int num = -1; /**/; num = -1)
    {
        for (int i = 0; i < lsize; ++i)
            distances[i] = singleExpansion(x_n, i);

        int minIndex = min_idx(distances);
        double minValue = distances[minIndex];

        if (minValue < 0.98*optValue)
            optValue = distances[num = minIndex];

        if (num == -1)
            break;
        labelings[num].copyTo(x_n);
    }
}

template <typename Tp> void Photomontage <Tp>::
assignLabeling(cv::Mat &img)
{
    if (multiscale == 0 || (height < EFFECTIVE_HEIGHT && width < EFFECTIVE_WIDTH))
    {
        img.create(height, width, CV_32SC1);

        img.setTo(0);
        gradientDescent(img, img);
    }
    else
    {
        int l = std::min( cvRound(height/600.0), cvRound(width/800.0) );
        img.create( cv::Size(width/l, height/l), CV_32SC1 );

        ...
        img.setTo(0);
        gradientDescent(img, img);

        resize(img, img, cv::Size(height, width), 0.0, 0.0, cv::INTER_NEAREST);

        ...
    }
}

template <typename Tp> void Photomontage <Tp>::
assignResImage(cv::Mat &img)
{
    cv::Mat optimalLabeling;
    assignLabeling(optimalLabeling);

    img.create( height, width, type );

    for (int i = 0; i < height; ++i)
        for (int j = 0; j < width; ++j)
            img.at<Tp>(i, j) = images[ optimalLabeling.at<int>(i, j) ].at<Tp>(i, j);
}

template <typename Tp> Photomontage <Tp>::
Photomontage(const std::vector <cv::Mat> &images, const std::vector <cv::Mat> &masks, const bool multiscale)
  :
    images(images), masks(masks), multiscale(multiscale), height(images[0].rows), width(images[0].cols), type(images[0].type()),
    channels(images[0].channels()), lsize(images.size()), labelings(images.size()), distances(images.size())
{
    CV_Assert(images[0].depth() != CV_8U && masks[0].depth() == CV_8U);
}

#endif /* __PHOTOMONTAGE_HPP__ */