#include <gtest/gtest.h>

#include "instance/game/Board.h"

using namespace nhn;
using namespace nhn::instance;

namespace {

/// Fills a straight line of @p length starting at (x, y).
void PlaceRun(Board& board, uint8 x, uint8 y, int32 dx, int32 dy, uint8 length, uint8 slot) {
    for (uint8 i = 0; i < length; ++i) {
        const auto cx = static_cast<uint8>(x + dx * i);
        const auto cy = static_cast<uint8>(y + dy * i);
        ASSERT_TRUE(board.Claim(board.CellIndex(cx, cy), slot));
    }
}

}  // namespace

TEST(Board, ClaimTakesACellExactlyOnce) {
    Board board;
    board.Reset(3, 3, 3);

    EXPECT_TRUE(board.IsEmpty(4));
    EXPECT_TRUE(board.Claim(4, 0));
    EXPECT_FALSE(board.IsEmpty(4));
    EXPECT_EQ(board.OwnerAt(4), 0);

    // The whole contest in simultaneous play: whoever gets there first keeps it.
    EXPECT_FALSE(board.Claim(4, 1));
    EXPECT_EQ(board.OwnerAt(4), 0);
}

TEST(Board, RejectsOutOfRangeCells) {
    Board board;
    board.Reset(3, 3, 3);
    EXPECT_FALSE(board.Claim(9, 0));
    EXPECT_FALSE(board.Claim(Board::kNoCell, 0));
    EXPECT_EQ(board.OwnerAt(9), Board::kNoOwner);
}

TEST(Board, DetectsLinesInEveryDirection) {
    struct Case {
        const char* name;
        int32 dx;
        int32 dy;
        uint8 startX;
        uint8 startY;
    };
    const Case cases[] = {
        {"horizontal", 1, 0, 0, 4},
        {"vertical", 0, 1, 4, 0},
        {"diagonal down", 1, 1, 0, 0},
        {"diagonal up", 1, -1, 0, 8},
    };

    for (const Case& testCase : cases) {
        Board board;
        board.Reset(9, 9, 4);

        // Three marks are not enough on a four-in-a-row board.
        PlaceRun(board, testCase.startX, testCase.startY, testCase.dx, testCase.dy, 3, 1);
        const uint16 third = board.CellIndex(
            static_cast<uint8>(testCase.startX + testCase.dx * 2),
            static_cast<uint8>(testCase.startY + testCase.dy * 2));
        EXPECT_FALSE(board.CompletesLine(third, 1)) << testCase.name;

        const auto fourthX = static_cast<uint8>(testCase.startX + testCase.dx * 3);
        const auto fourthY = static_cast<uint8>(testCase.startY + testCase.dy * 3);
        const uint16 fourth = board.CellIndex(fourthX, fourthY);
        ASSERT_TRUE(board.Claim(fourth, 1));
        EXPECT_TRUE(board.CompletesLine(fourth, 1)) << testCase.name;
    }
}

TEST(Board, CountsARunThroughTheNewMarkFromBothSides) {
    // The completing mark lands in the middle of a gap, which only counts if
    // both directions are walked.
    Board board;
    board.Reset(9, 9, 4);

    ASSERT_TRUE(board.Claim(board.CellIndex(2, 3), 2));
    ASSERT_TRUE(board.Claim(board.CellIndex(3, 3), 2));
    ASSERT_TRUE(board.Claim(board.CellIndex(5, 3), 2));

    const uint16 middle = board.CellIndex(4, 3);
    ASSERT_TRUE(board.Claim(middle, 2));
    EXPECT_TRUE(board.CompletesLine(middle, 2));
}

TEST(Board, DoesNotCountAnotherPlayersMarks) {
    Board board;
    board.Reset(9, 9, 4);

    ASSERT_TRUE(board.Claim(board.CellIndex(0, 0), 0));
    ASSERT_TRUE(board.Claim(board.CellIndex(1, 0), 0));
    ASSERT_TRUE(board.Claim(board.CellIndex(2, 0), 1));  // opponent breaks it

    const uint16 fourth = board.CellIndex(3, 0);
    ASSERT_TRUE(board.Claim(fourth, 0));
    EXPECT_FALSE(board.CompletesLine(fourth, 0));
}

TEST(Board, DoesNotWrapAroundTheEdge) {
    // Cells 8 and 9 are adjacent in memory but on opposite sides of a 9-wide
    // board; a run must not read across the seam.
    Board board;
    board.Reset(9, 9, 4);

    ASSERT_TRUE(board.Claim(board.CellIndex(7, 0), 0));
    ASSERT_TRUE(board.Claim(board.CellIndex(8, 0), 0));
    ASSERT_TRUE(board.Claim(board.CellIndex(0, 1), 0));
    ASSERT_TRUE(board.Claim(board.CellIndex(1, 1), 0));

    EXPECT_FALSE(board.CompletesLine(board.CellIndex(1, 1), 0));
}

TEST(Board, TicTacToeNeedsThreeAndGomoku15NeedsFive) {
    Board tictactoe;
    tictactoe.Reset(3, 3, 3);
    PlaceRun(tictactoe, 0, 1, 1, 0, 2, 0);
    EXPECT_FALSE(tictactoe.CompletesLine(tictactoe.CellIndex(1, 1), 0));
    ASSERT_TRUE(tictactoe.Claim(tictactoe.CellIndex(2, 1), 0));
    EXPECT_TRUE(tictactoe.CompletesLine(tictactoe.CellIndex(2, 1), 0));

    Board gomoku;
    gomoku.Reset(15, 15, 5);
    PlaceRun(gomoku, 3, 7, 1, 0, 4, 1);
    EXPECT_FALSE(gomoku.CompletesLine(gomoku.CellIndex(6, 7), 1));
    ASSERT_TRUE(gomoku.Claim(gomoku.CellIndex(7, 7), 1));
    EXPECT_TRUE(gomoku.CompletesLine(gomoku.CellIndex(7, 7), 1));
}

TEST(Board, ReportsFullOnlyWhenEveryCellIsTaken) {
    Board board;
    board.Reset(3, 3, 3);
    EXPECT_FALSE(board.IsFull());

    for (uint16 cell = 0; cell < board.CellCount(); ++cell) {
        // Alternating owners, so no line is completed on the way.
        ASSERT_TRUE(board.Claim(cell, static_cast<uint8>(cell % 2)));
    }
    EXPECT_TRUE(board.IsFull());
}

TEST(Board, ResetClearsPreviousMarks) {
    // The board persists across waves but not across rounds.
    Board board;
    board.Reset(3, 3, 3);
    ASSERT_TRUE(board.Claim(0, 0));

    board.Reset(9, 9, 4);
    EXPECT_EQ(board.Width(), 9);
    EXPECT_TRUE(board.IsEmpty(0));
    EXPECT_FALSE(board.IsFull());
}
