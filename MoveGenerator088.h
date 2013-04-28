#include "chess.h"

#ifdef __CUDACC__
#define CUDA_CALLABLE_MEMBER __host__ __device__
#else
#define CUDA_CALLABLE_MEMBER
#endif

class MoveGenerator
{
private:
    CUDA_CALLABLE_MEMBER __forceinline static bool MoveGenerator::isInvalidMove(BoardPosition *pos, const uint8 chance, uint32 src, uint32 dst, uint8 oldPiece, uint8 flags, uint8 kingPos)
    {
        // a move is illegal if it puts king in check! (or doesn't take the king out of check)
        if (ISVALIDPOS(kingPos))
        {
            // remove the pieces from source/dst positions to correctly check
            uint8 temp = pos->board[src];
            pos->board[dst] = temp;
            pos->board[src] = 0;
            uint8 enPassentCaptureLocation = 0;

            if (flags == EN_PASSENT)
            {
                enPassentCaptureLocation = INDEX088(RANK(src), FILE(dst));
                pos->board[enPassentCaptureLocation] = 0;
            }

            bool illegal = isThreatened(pos, kingPos, !chance);

            pos->board[src] = temp;
            if (flags == EN_PASSENT)
            {
                pos->board[dst] = 0;
                pos->board[enPassentCaptureLocation] = oldPiece;
            }
            else
            {
                pos->board[dst] = oldPiece;	
            }

            return illegal;
        }
        else
        {
            // remove the king to check correctly for threats
            uint8 temp = pos->board[src];
            pos->board[src] = 0;

            // the king has moved
            bool illegal = isThreatened(pos, dst, !chance);

            pos->board[src] = temp;
            
            return illegal;
        }
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void addMove(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, uint32 src, uint32 dst, uint8 oldPiece, uint8 flags, uint8 kingPos)
    {
        // check if the move would put the king in check
        if (isInvalidMove(pos, chance, src, dst, oldPiece, flags, kingPos))
        {
            return;
        }

        moves[*nMoves].src = (uint8) src;
        moves[*nMoves].dst = (uint8) dst;
        moves[*nMoves].capturedPiece = (uint8) oldPiece;
        moves[*nMoves].flags = flags;

        (*nMoves)++;
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void addPromotions(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, uint32 src, uint32 dst, uint8 oldPiece, uint8 kingPos)
    {
        addMove(pos, moves, nMoves, chance, src, dst, oldPiece, PROMOTION_QUEEN, kingPos);
        addMove(pos, moves, nMoves, chance, src, dst, oldPiece, PROMOTION_KNIGHT, kingPos);
        addMove(pos, moves, nMoves, chance, src, dst, oldPiece, PROMOTION_ROOK, kingPos);
        addMove(pos, moves, nMoves, chance, src, dst, oldPiece, PROMOTION_BISHOP, kingPos);
    }
    CUDA_CALLABLE_MEMBER __forceinline  static void generatePawnMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8 kingPos)
    {
        uint32 finalRank = chance ?  0 : 7;
        uint32 curRank   = RANK(curPos);

        // pawn advancement
        
        // single square forward
        uint32 offset  = chance ? -16 : 16;
        uint32 newPos  = curPos + offset;
        uint32 newRank = RANK(newPos);

        if (ISVALIDPOS(newPos) && ISEMPTY(pos->board[newPos]))
        {
            if(newRank == finalRank)
            {
                // promotion
                addPromotions(pos, moves, nMoves, chance, curPos, newPos, EMPTY_SQUARE, kingPos);
            }
            else
            {
                addMove(pos, moves, nMoves, chance, curPos, newPos, EMPTY_SQUARE, 0, kingPos);

                // two squares forward
                uint32 startRank = chance ?  6 : 1;
                if (curRank == startRank)
                {
                    newPos += offset;
                    if (ISEMPTY(pos->board[newPos]))
                        addMove(pos, moves, nMoves, chance, curPos, newPos, EMPTY_SQUARE, 0, kingPos);
                }
            }
        }

        // captures
        offset = chance ? -15 : 15;
        newPos = curPos + offset;
        uint32 capturedPiece = pos->board[newPos];
        if (ISVALIDPOS(newPos) && IS_ENEMY_COLOR(capturedPiece, chance))
        {
            if(newRank == finalRank)
            {
                // promotion
                addPromotions(pos, moves, nMoves, chance, curPos, newPos, capturedPiece, kingPos);
            }
            else
            {
                addMove(pos, moves, nMoves, chance, curPos, newPos, capturedPiece, 0, kingPos);
            }
        }

        offset = chance ? -17 : 17;
        newPos = curPos + offset;
        capturedPiece = pos->board[newPos];
        if (ISVALIDPOS(newPos) && IS_ENEMY_COLOR(capturedPiece, chance))
        {
            if(newRank == finalRank)
            {
                // promotion
                addPromotions(pos, moves, nMoves, chance, curPos, newPos, capturedPiece, kingPos);
            }
            else
            {
                addMove(pos, moves, nMoves, chance, curPos, newPos, capturedPiece, 0, kingPos);
            }
        }

        // En-passent
        if (pos->enPassent)
        {
            uint32 enPassentFile = pos->enPassent - 1;
            uint32 enPassentRank = chance ? 3 : 4;
            if ((curRank == enPassentRank) && (abs(int(FILE(curPos)) - int(enPassentFile)) == 1))
            {
                uint32 finalRank = chance ? 2 : 5;
                newPos = INDEX088(finalRank, enPassentFile);
                addMove(pos, moves, nMoves, chance, curPos, newPos, COLOR_PIECE(!chance, PAWN), EN_PASSENT, kingPos);
            }
        }
        
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateOffsetedMove(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, uint32 curPos, uint32 offset, uint32 kingPos)
    {
        uint32 newPos = curPos + offset;
        if(ISVALIDPOS(newPos))
        {
            uint8 capturedPiece = pos->board[newPos];
            if (!IS_OF_COLOR(capturedPiece, chance))
                addMove(pos, moves, nMoves, chance, curPos, newPos, capturedPiece, 0, kingPos);
        }
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateOffsetedMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, 
                                                    const uint32 jumpTable[], int n, uint32 kingPos)
    {
        for (int i = 0; i < n; i++)
        {
            generateOffsetedMove(pos, moves, nMoves, chance, curPos, jumpTable[i], kingPos);
        }        
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateKnightMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8 kingPos)
    {
        const uint32 jumpTable[] = {0x1F, 0x21, 0xE, 0x12, -0x12, -0xE, -0x21, -0x1F};
        generateOffsetedMoves(pos, moves, nMoves, chance, curPos, jumpTable, 8, kingPos);
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateKingMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8)
    {
        // normal moves
        const uint32 jumpTable[] = {0xF, 0x10, 0x11, 0x1, -0x1, -0x11, -0x10, -0xF};
        generateOffsetedMoves(pos, moves, nMoves, chance, curPos, jumpTable, 8, 0xFF);

        // castling
        uint32 castleFlag = chance ? pos->blackCastle : pos->whiteCastle ;

        // no need to check for king's and rook's position as if they have moved, the castle flag would be zero
        if ((castleFlag & CASTLE_FLAG_KING_SIDE) && ISEMPTY(pos->board[curPos+1]) && ISEMPTY(pos->board[curPos+2]))
        {
            if (!(isThreatened(pos, curPos, !chance) || isThreatened(pos, curPos+1, !chance) ||
                  isThreatened(pos, curPos+2, !chance)))
            {
                addMove(pos, moves, nMoves, chance, curPos, curPos + 0x2, EMPTY_SQUARE, CASTLE_KING_SIDE, 0xFF);
            }
        }
        if ((castleFlag & CASTLE_FLAG_QUEEN_SIDE) && ISEMPTY(pos->board[curPos-1]) && 
            ISEMPTY(pos->board[curPos-2]) && ISEMPTY(pos->board[curPos-3]))
        {
            if (!(isThreatened(pos, curPos, !chance) || isThreatened(pos, curPos-1, !chance) || 
                  isThreatened(pos, curPos-2, !chance)))
            {
                addMove(pos, moves, nMoves, chance, curPos, curPos - 0x2, EMPTY_SQUARE, CASTLE_QUEEN_SIDE, 0xFF);
            }
        }
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateSlidingMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8 kingPos, const uint32 offset)
    {
        uint32 newPos = curPos;
        while(1)
        {
            newPos += offset;
            if (ISVALIDPOS(newPos))
            {
                uint32 oldPiece = pos->board[newPos];
                if (!ISEMPTY(oldPiece))
                {
                    if(!IS_OF_COLOR(oldPiece,chance))
                    {
                        addMove(pos, moves, nMoves, chance, curPos, newPos, oldPiece, 0, kingPos);
                    }
                    break;
                }
                else
                {
                    addMove(pos, moves, nMoves, chance, curPos, newPos, EMPTY_SQUARE, 0, kingPos);
                }
            }
            else
            {
                break;
            }
        }        
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateRookMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8 kingPos)
    {
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos,  0x10);    // up
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos, -0x10);    // down
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos,   0x1);    // right
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos,  -0x1);    // left
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateBishopMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8 kingPos)
    {
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos,   0xf);    // north-west
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos,  0x11);    // north-east
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos, -0x11);    // south-west
        generateSlidingMoves(pos, moves, nMoves, chance, curPos, kingPos,  -0xf);    // south-east
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateQueenMoves(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 curPos, const uint8 kingPos)
    {
        generateRookMoves(pos, moves, nMoves, chance, curPos, kingPos);
        generateBishopMoves(pos, moves, nMoves, chance, curPos, kingPos);
    }

	CUDA_CALLABLE_MEMBER __forceinline  static bool checkSlidingThreat(const BoardPosition *pos, uint32 curPos, uint32 offset, uint32 piece1, uint32 piece2)
    {
        uint32 newPos = curPos;

        while(1)
        {
            newPos += offset;
            if (ISVALIDPOS(newPos))
            {
                uint32 piece = pos->board[newPos];
                if (!ISEMPTY(piece))
                {
                    if((piece == piece1) || (piece == piece2))
                        return true;
                    else
                        return false;
                }
            }
            else
            {
                return false;
            }
        }
    }

    // checks if the position is under attack by a piece of 'color'
	CUDA_CALLABLE_MEMBER static bool isThreatened(const BoardPosition *pos, const uint32 curPos, uint32 color)
    {
        // check if threatened by pawns
        uint32 pieceToCheck = COLOR_PIECE(color, PAWN);
        uint32 offset   = color ? 15 : -15;
        uint32 piecePos = curPos + offset;
        if (ISVALIDPOS(piecePos) && (pos->board[piecePos] == pieceToCheck))
        {
            return true;
        }

        offset   = color ? 17 : -17;
        piecePos = curPos + offset;
        if (ISVALIDPOS(piecePos) && (pos->board[piecePos] == pieceToCheck))
            return true;
        
        // check if threatened by knights
        pieceToCheck = COLOR_PIECE(color, KNIGHT);
        const uint32 jumpTableKnights[] = {0x1F, 0x21, 0xE, 0x12, -0x12, -0xE, -0x21, -0x1F};
        for (uint32 i=0; i < 8; i++)
        {
            offset   = jumpTableKnights[i];
            piecePos = curPos + offset;
            if (ISVALIDPOS(piecePos) && (pos->board[piecePos] == pieceToCheck))
                return true;
        }

        // check if threatened by king
        pieceToCheck = COLOR_PIECE(color, KING);
        const uint32 jumpTableKings[] = {0xF, 0x10, 0x11, 0x1, -0x1, -0x11, -0x10, -0xF};
        for (uint32 i=0; i < 8; i++)
        {
            offset   = jumpTableKings[i];
            piecePos = curPos + offset;
            if (ISVALIDPOS(piecePos) && (pos->board[piecePos] == pieceToCheck))
                return true;
        }

        // check if threatened by rook (or queen)
        pieceToCheck = COLOR_PIECE(color, ROOK);
        uint32 pieceToCheck2 = COLOR_PIECE(color, QUEEN);
        if(checkSlidingThreat(pos, curPos,  0x10, pieceToCheck, pieceToCheck2)) // up
            return true;
        if(checkSlidingThreat(pos, curPos, -0x10, pieceToCheck, pieceToCheck2)) // down
            return true;
        if(checkSlidingThreat(pos, curPos,   0x1, pieceToCheck, pieceToCheck2)) // right
            return true;
        if(checkSlidingThreat(pos, curPos,  -0x1, pieceToCheck, pieceToCheck2))  // left
            return true;

        // check if threatened by bishop (or queen)
        pieceToCheck = COLOR_PIECE(color, BISHOP);
        if(checkSlidingThreat(pos, curPos,   0xf, pieceToCheck, pieceToCheck2)) // north-west
            return true;
        if(checkSlidingThreat(pos, curPos,  0x11, pieceToCheck, pieceToCheck2)) // north-east
            return true;
        if(checkSlidingThreat(pos, curPos, -0x11, pieceToCheck, pieceToCheck2)) // south-west
            return true;
        if(checkSlidingThreat(pos, curPos,  -0xf, pieceToCheck, pieceToCheck2)) // south-east
            return true;

        return false;
    }

    CUDA_CALLABLE_MEMBER __forceinline  static void generateMovesForSquare(BoardPosition *pos, Move *moves, uint32 *nMoves, const uint8 chance, const uint8 index088, const uint8 colorpiece, const uint8 kingPos)
    {
        uint8 piece = PIECE(colorpiece);
        
        switch(piece)
        {
            case PAWN:
                return generatePawnMoves(pos, moves, nMoves, chance, index088, kingPos);
            case KNIGHT:
                return generateKnightMoves(pos, moves, nMoves, chance, index088, kingPos);
            case BISHOP:
                return generateBishopMoves(pos, moves, nMoves, chance, index088, kingPos);
            case ROOK:
                return generateRookMoves(pos, moves, nMoves, chance, index088, kingPos);
            case QUEEN:
                return generateQueenMoves(pos, moves, nMoves, chance, index088, kingPos);
            case KING:
                return generateKingMoves(pos, moves, nMoves, chance, index088, kingPos);
        }
    }

public:
    // generates moves for the given board position
    // returns the no of moves generated
    CUDA_CALLABLE_MEMBER static int generateMoves (BoardPosition *pos, Move *moves)
    {
        uint32 nMoves = 0;
        uint8  chance = pos->chance;

       
        uint8 i, j;

        // TODO: we don't need to find this everytime! Keep this in board structure and update when making move
        uint8 kingPiece = COLOR_PIECE(chance, KING);
        uint8 kingPos = 0xFF;
        for (i = 0; i < 128; i++)
            if (ISVALIDPOS(i) && pos->board[i] == kingPiece)
                kingPos = i;

        for (i = 0; i < 8; i++)
        {
            for (j = 0; j < 8; j++)
            {
                uint32 index088 = INDEX088(i, j);
                uint32 piece = pos->board[index088];
                if(piece && (COLOR(piece) == chance))
                {
                    generateMovesForSquare(pos, moves, &nMoves, chance, index088, piece, kingPos);
                }
            }
        }

        return nMoves;
    }

};
