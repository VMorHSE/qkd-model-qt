//
// Created by Vladimir Morozov on 02.09.2022.
//

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#include <vector>
#include <list>
#include <cstring>
#include <random>

#include "Matrix.h"
#include "common.h"
#include "LDPC.h"

#ifndef LDPCPARTICULARSYNDROME_LDPCCORRECT_H
#define LDPCPARTICULARSYNDROME_LDPCCORRECT_H

#define ABS(A) (((A) >= 0) ? (A) : -(A))
#define HARD(A) (((A) < 0)?1:0)
#define OFFSET(A, B) (((A) > (B)) ? ((A)-(B)) : 0)
#define INFTY 1000000

using namespace std;

double safe_exp(double x)
{
    if (x > 700)
    {
        return 1.014232054735005e+304;
    }
    if (x < -700)
    {
        return 9.859676543759770e-305;
    }
    return exp(x);
}

double safe_log(double x)
{
    if (x < 9.859676543759770e-305)
    {
        return -700;
    }
    return log(x);
}


double phi(double x)
{
    static const double lim_tanh = 31.0;
    static const double min_tanh = 6.883382752676208e-14; //log( (exp((double)lim) + 1)/(exp((double)lim) - 1));

    if (x > lim_tanh)
    {
        return 2*safe_exp(-x);
    }

    //if (x < min_tanh)
    //{
    //  return -safe_log(x/2);
    //}

    return -safe_log(tanh(x/2));
}


// Sum-Product Decoder
bool SumProduct(LDPC& ldpc, vector<double>& in_llr, int max_iter, vector<double>& out_llr, vector<int>& y, int* number_of_iter)
{
    // Auxiliary matrices
    Matrix<double>  R_msgs(ldpc.m, ldpc.rmax); // messages from check to variable nodes
    Matrix<double>  Q_tanhs(ldpc.m, ldpc.rmax);
    Matrix<int>  Q_signs(ldpc.m, ldpc.rmax);
    double sum_tanhs = 0;
    int sum_sign = 0;
    int sign = 0;
    double temp = 0;


    // Initialization
    for (int i = 0; i < ldpc.n; ++i)
    {
        out_llr[i] = in_llr[i];
        y[i] = HARD(out_llr[i]);

        for (int j = 0; j < ldpc.col_weight[i]; ++j)
        {
            Q_signs(ldpc.msgs_col(i,j)) = 0;
            if (in_llr[i] < 0)
            {
                Q_signs(ldpc.msgs_col(i,j)) = 1;
            }
            Q_tanhs(ldpc.msgs_col(i,j)) = phi(fabs(in_llr[i]));
        }
    }

    for (int loop = 0; loop < max_iter; ++loop)
    {
        // Update R
        for (int j = 0; j < ldpc.m; j++)
        {
            sum_tanhs = 0;
            sum_sign = 0;

            for (int k = 0; k < ldpc.row_weight[j]; k++)
            {
                sum_tanhs += Q_tanhs(j, k);
                sum_sign ^= Q_signs(j, k);
            }
            for (int k = 0; k < ldpc.row_weight[j]; k++)
            {
                sign = sum_sign^Q_signs(j, k);
                R_msgs(j,k) = (1-2*sign)*phi(sum_tanhs - Q_tanhs(j, k));
            }
        }
        // Update Q
        for (int i = 0; i < ldpc.n; i++)
        {
            out_llr[i] = in_llr[i];

            for (int k = 0; k < ldpc.col_weight[i]; k++)
            {
                out_llr[i] += R_msgs(ldpc.msgs_col(i,k));
            }

            y[i] = HARD(out_llr[i]);

            for (int k = 0; k < ldpc.col_weight[i]; k++)
            {
                temp = out_llr[i] - R_msgs(ldpc.msgs_col(i,k));
                Q_signs(ldpc.msgs_col(i,k)) = 0;
                if (temp < 0)
                {
                    Q_signs(ldpc.msgs_col(i,k)) = 1;
                }
                Q_tanhs(ldpc.msgs_col(i,k)) = phi(fabs(temp));
            }
        }
    }

    if (number_of_iter)
    {
        *number_of_iter = max_iter;
    }
    return 1;
}


bool LDPCCorrect(const std::vector<int>& neededSyndrome, const std::vector<int>& noisedData, std::vector<int>& clearedData, double epsilon)
{
    LDPC ldpcForErrorCorrection;
    ldpcForErrorCorrection.init("H_with_padding.alist");

//    const double epsilon = 0.03;

    Matrix<FieldElement> neededSyndromeMatrix(1, neededSyndrome.size());
    for (size_t i = 0; i < neededSyndromeMatrix.getColumnsNumber(); ++i)
    {
        neededSyndromeMatrix(0, i) = FieldElement(neededSyndrome[i]);
    }

    std::vector<double> concatenatedLLRs;
    concatenatedLLRs.reserve(noisedData.size() + neededSyndromeMatrix.getColumnsNumber());

    for (size_t i = 0; i < noisedData.size(); ++i)
    {
        concatenatedLLRs.push_back(noisedData[i] == 0 ? log((1.0 - epsilon) / epsilon) : -log((1.0 - epsilon) / epsilon));
    }
    for (size_t i = 0; i < neededSyndromeMatrix.getColumnsNumber(); ++i)
    {
        concatenatedLLRs.push_back(neededSyndromeMatrix(0, i).getElement() == 0 ? 1000 : -1000);
    }

    std::vector<double> out_llr(concatenatedLLRs.size());
    int number_of_iter{0};
    std::vector<int> y(concatenatedLLRs.size());
    bool success = SumProduct(ldpcForErrorCorrection, concatenatedLLRs, 50, out_llr, y, &number_of_iter);

    clearedData = std::vector<int>(noisedData.size());
    std::copy(y.begin(), y.begin() + noisedData.size(), clearedData.begin());

    return success;
}


bool LDPCCorrectWithExposedBits(const std::vector<int>& neededSyndrome,
                                const std::vector<int>& noisedData,
                                std::vector<int>& clearedData,
                                double epsilon,
                                const std::vector<size_t>& positions,
                                const std::vector<uint8_t>& bits)
{
    LDPC ldpcForErrorCorrection;
    ldpcForErrorCorrection.init("H_with_padding.alist");

//    const double epsilon = 0.03;

    Matrix<FieldElement> neededSyndromeMatrix(1, neededSyndrome.size());
    for (size_t i = 0; i < neededSyndromeMatrix.getColumnsNumber(); ++i)
    {
        neededSyndromeMatrix(0, i) = FieldElement(neededSyndrome[i]);
    }

    std::vector<double> concatenatedLLRs;
    concatenatedLLRs.reserve(noisedData.size() + neededSyndromeMatrix.getColumnsNumber());

    for (size_t i = 0; i < noisedData.size(); ++i)
    {
        concatenatedLLRs.push_back(noisedData[i] == 0 ? log((1.0 - epsilon) / epsilon) : -log((1.0 - epsilon) / epsilon));
    }
    for (size_t i = 0; i < neededSyndromeMatrix.getColumnsNumber(); ++i)
    {
        concatenatedLLRs.push_back(neededSyndromeMatrix(0, i).getElement() == 0 ? 1000 : -1000);
    }
    for (size_t i = 0; i < positions.size(); ++i)
    {
        concatenatedLLRs[positions[i]] = (bits[i] == 0 ? 1000 : -1000);
    }

    std::vector<double> out_llr(concatenatedLLRs.size());
    int number_of_iter{0};
    std::vector<int> y(concatenatedLLRs.size());
    bool success = SumProduct(ldpcForErrorCorrection, concatenatedLLRs, 50, out_llr, y, &number_of_iter);

    clearedData = std::vector<int>(noisedData.size());
    std::copy(y.begin(), y.begin() + noisedData.size(), clearedData.begin());

    return success;
}

#endif //LDPCPARTICULARSYNDROME_LDPCCORRECT_H
