#include "colour.h"
#include "square.h"
#include <types.h>
#include <assert.h>

namespace LIBHeisenKernel
{
    namespace engine
    {
        /**
         * @brief Functions are defined here
        */
        const int SquareFrom64[64] = {
        A1, B1, C1, D1, E1, F1, G1, H1,
        A2, B2, C2, D2, E2, F2, G2, H2,
        A3, B3, C3, D3, E3, F3, G3, H3,
        A4, B4, C4, D4, E4, F4, G4, H4,
        A5, B5, C5, D5, E5, F5, G5, H5,
        A6, B6, C6, D6, E6, F6, G6, H6,
        A7, B7, C7, D7, E7, F7, G7, H7,
        A8, B8, C8, D8, E8, F8, G8, H8,
        };
        const int RankMask[ColourNb] = { 0, 0xF };
        const int PromoteRank[ColourNb] = { 0xB0, 0x40 };

        int SquareTo64[SquareNb];
        bool SquareIsPromote[SquareNb];

        void square_init(){
            int sq;
            for (sq = 0; sq < SquareNb; sq++) SquareTo64[sq] = -1;
        

            for (sq = 0; sq < 64; sq++) {
                SquareTo64[SquareFrom64[sq]] = sq;
            }
            for (sq = 0; sq < SquareNb; sq++) {
        SquareIsPromote[sq] = SQUARE_IS_OK(sq) && (SQUARE_RANK(sq) == Rank1 || SQUARE_RANK(sq) == Rank8);
        }}
        int file_from_char(int c) {

        assert(c>='a'&&c<='h');

        return FileA + (c - 'a');
        }

    }



}