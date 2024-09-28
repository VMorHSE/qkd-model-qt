//
// Created by Vladimir Morozov on 23.09.2022.
//

#ifndef KEYENHANCER_GALOISDIVIDER_HPP
#define KEYENHANCER_GALOISDIVIDER_HPP

#include "GaloisField.h"
#include "GaloisFieldElement.h"
#include "GaloisFieldPolynomial.h"

class GaloisComputer
{
public:
    GaloisComputer()
    {
        m_gf = new galois::GaloisField(1, m_primPolCoeffs);
    }

    ~GaloisComputer()
    {
        delete m_gf;
    }

    std::vector<uint8_t> GetRemainder(const std::vector<uint8_t>& pol1, const std::vector<uint8_t>& pol2)
    {
        galois::GaloisFieldElement gfe1[pol1.size()];
        for (size_t i = 0; i < pol1.size(); ++i)
        {
            gfe1[i] = galois::GaloisFieldElement(m_gf, pol1[pol1.size() - 1 - i]);
        }
        galois::GaloisFieldPolynomial poly1(m_gf, pol1.size() - 1, gfe1);

        galois::GaloisFieldElement gfe2[pol1.size()];
        for (size_t i = 0; i < pol2.size(); ++i)
        {
            gfe2[i] = galois::GaloisFieldElement(m_gf, pol2[pol2.size() - 1 - i]);
        }
        galois::GaloisFieldPolynomial poly2(m_gf, pol2.size() - 1, gfe2);

        galois::GaloisFieldPolynomial poly3;

        poly3 = poly1 % poly2;

        std::vector<uint8_t> res(poly3.deg() + 1);
        for (size_t i = 0; i < poly3.deg() + 1; ++i)
        {
            res[i] = poly3[poly3.deg() - i].poly();
        }

        return res;
    }

    std::vector<uint8_t> Multiply(const std::vector<uint8_t>& pol1, const std::vector<uint8_t>& pol2)
    {
        galois::GaloisFieldElement gfe1[pol1.size()];
        for (size_t i = 0; i < pol1.size(); ++i)
        {
            gfe1[i] = galois::GaloisFieldElement(m_gf, pol1[pol1.size() - 1 - i]);
        }
        galois::GaloisFieldPolynomial poly1(m_gf, pol1.size() - 1, gfe1);

        galois::GaloisFieldElement gfe2[pol1.size()];
        for (size_t i = 0; i < pol2.size(); ++i)
        {
            gfe2[i] = galois::GaloisFieldElement(m_gf, pol2[pol2.size() - 1 - i]);
        }
        galois::GaloisFieldPolynomial poly2(m_gf, pol2.size() - 1, gfe2);

        galois::GaloisFieldPolynomial poly3;

        poly3 = poly1 * poly2;

        std::vector<uint8_t> res(poly3.deg() + 1);
        for (size_t i = 0; i < poly3.deg() + 1; ++i)
        {
            res[i] = poly3[poly3.deg() - i].poly();
        }

        return res;
    }

private:
    galois::GaloisField *           m_gf;
    unsigned int                    m_primPolCoeffs[5] {1,0,0,1,1};
};


#endif //KEYENHANCER_GALOISDIVIDER_HPP
