﻿//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "Resize.hpp"

#include "TensorBufferArrayView.hpp"

#include <boost/numeric/conversion/cast.hpp>

#include <cmath>
#include <algorithm>

using namespace armnnUtils;

namespace armnn
{

namespace
{

inline float Lerp(float a, float b, float w)
{
    return w * b + (1.f - w) * a;
}

}// anonymous namespace

void Resize(Decoder<float>&   in,
            const TensorInfo& inputInfo,
            Encoder<float>&   out,
            const TensorInfo& outputInfo,
            DataLayoutIndexed dataLayout,
            armnn::ResizeMethod resizeMethod)
{
    // We follow the definition of TensorFlow and AndroidNN: the top-left corner of a texel in the output
    // image is projected into the input image to figure out the interpolants and weights. Note that this
    // will yield different results than if projecting the centre of output texels.

    const unsigned int batchSize = inputInfo.GetShape()[0];
    const unsigned int channelCount = inputInfo.GetShape()[dataLayout.GetChannelsIndex()];

    const unsigned int inputHeight = inputInfo.GetShape()[dataLayout.GetHeightIndex()];
    const unsigned int inputWidth = inputInfo.GetShape()[dataLayout.GetWidthIndex()];
    const unsigned int outputHeight = outputInfo.GetShape()[dataLayout.GetHeightIndex()];
    const unsigned int outputWidth = outputInfo.GetShape()[dataLayout.GetWidthIndex()];

    // How much to scale pixel coordinates in the output image, to get the corresponding pixel coordinates
    // in the input image.
    const float scaleY = boost::numeric_cast<float>(inputHeight) / boost::numeric_cast<float>(outputHeight);
    const float scaleX = boost::numeric_cast<float>(inputWidth) / boost::numeric_cast<float>(outputWidth);

    TensorShape inputShape =  inputInfo.GetShape();
    TensorShape outputShape =  outputInfo.GetShape();

    for (unsigned int n = 0; n < batchSize; ++n)
    {
        for (unsigned int c = 0; c < channelCount; ++c)
        {
            for (unsigned int y = 0; y < outputHeight; ++y)
            {
                // Corresponding real-valued height coordinate in input image.
                const float iy = boost::numeric_cast<float>(y) * scaleY;

                // Discrete height coordinate of top-left texel (in the 2x2 texel area used for interpolation).
                const float fiy = floorf(iy);
                const unsigned int y0 = boost::numeric_cast<unsigned int>(fiy);

                // Interpolation weight (range [0,1]).
                const float yw = iy - fiy;

                for (unsigned int x = 0; x < outputWidth; ++x)
                {
                    // Real-valued and discrete width coordinates in input image.
                    const float ix = boost::numeric_cast<float>(x) * scaleX;
                    const float fix = floorf(ix);
                    const unsigned int x0 = boost::numeric_cast<unsigned int>(fix);

                    // Interpolation weight (range [0,1]).
                    const float xw = ix - fix;

                    // Discrete width/height coordinates of texels below and to the right of (x0, y0).
                    const unsigned int x1 = std::min(x0 + 1, inputWidth - 1u);
                    const unsigned int y1 = std::min(y0 + 1, inputHeight - 1u);

                    float interpolatedValue;
                    switch (resizeMethod)
                    {
                        case armnn::ResizeMethod::Bilinear:
                        {
                            in[dataLayout.GetIndex(inputShape, n, c, y0, x0)];
                            float input1 = in.Get();
                            in[dataLayout.GetIndex(inputShape, n, c, y0, x1)];
                            float input2 = in.Get();
                            in[dataLayout.GetIndex(inputShape, n, c, y1, x0)];
                            float input3 = in.Get();
                            in[dataLayout.GetIndex(inputShape, n, c, y1, x1)];
                            float input4 = in.Get();

                            const float ly0 = Lerp(input1, input2, xw); // lerp along row y0.
                            const float ly1 = Lerp(input3, input4, xw); // lerp along row y1.
                            interpolatedValue = Lerp(ly0, ly1, yw);
                            break;
                        }
                        case armnn::ResizeMethod::NearestNeighbor:
                        default:
                        {
                            auto distance0 = std::sqrt(pow(fix - boost::numeric_cast<float>(x0), 2) + 
                                                       pow(fiy - boost::numeric_cast<float>(y0), 2));
                            auto distance1 = std::sqrt(pow(fix - boost::numeric_cast<float>(x1), 2) +
                                                       pow(fiy - boost::numeric_cast<float>(y1), 2));

                            unsigned int xNearest = distance0 <= distance1? x0 : x1;
                            unsigned int yNearest = distance0 <= distance1? y0 : y1;

                            in[dataLayout.GetIndex(inputShape, n, c, yNearest, xNearest)];
                            interpolatedValue = in.Get();
                            break;
                        }
                    }
                    out[dataLayout.GetIndex(outputShape, n, c, y, x)];
                    out.Set(interpolatedValue);
                }
            }
        }
    }
}

} //namespace armnn