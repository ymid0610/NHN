#include <gtest/gtest.h>

#include <array>

#include "protocol/GameTypes.h"
#include "protocol/Types.h"
#include "protocol/VoicePacket.h"

using namespace nhn;
using namespace nhn::proto;

TEST(GameModes, TableMatchesTheSpecifiedModes) {
    const GameModeDef* ttt = FindGameMode(GameMode::TicTacToe);
    ASSERT_NE(ttt, nullptr);
    EXPECT_EQ(ttt->boardWidth, 3);
    EXPECT_EQ(ttt->winLength, 3);
    EXPECT_EQ(ttt->capacity, 2);

    const GameModeDef* g9 = FindGameMode(GameMode::Gomoku9);
    ASSERT_NE(g9, nullptr);
    EXPECT_EQ(g9->boardWidth, 9);
    EXPECT_EQ(g9->winLength, 4);
    EXPECT_EQ(g9->capacity, 4);
    EXPECT_EQ(g9->minimumToStart, 2);  // gomoku runs 2-4, unlike tic-tac-toe

    const GameModeDef* g15 = FindGameMode(GameMode::Gomoku15);
    ASSERT_NE(g15, nullptr);
    EXPECT_EQ(g15->boardWidth, 15);
    EXPECT_EQ(g15->winLength, 5);

    EXPECT_FALSE(IsValidGameMode(GameMode::None));
    EXPECT_FALSE(IsValidGameMode(static_cast<GameMode>(99)));
}

TEST(GameModes, AmmoOptionsDifferPerMode) {
    // A 3x3 board cannot absorb six shots per player per wave — the first pass
    // would decide the round — so tic-tac-toe offers a tighter range.
    EXPECT_TRUE(IsAmmoAllowed(GameMode::TicTacToe, 2));
    EXPECT_FALSE(IsAmmoAllowed(GameMode::TicTacToe, 6));
    EXPECT_TRUE(IsAmmoAllowed(GameMode::Gomoku9, 6));
    EXPECT_FALSE(IsAmmoAllowed(GameMode::Gomoku9, 2));

    EXPECT_TRUE(IsAmmoAllowed(GameMode::TicTacToe, kUnlimited));
    EXPECT_TRUE(IsAmmoAllowed(GameMode::Gomoku15, kUnlimited));
}

TEST(GameModes, LargerBoardsGetALargerWaveCap) {
    // Five in a row on 15x15 is statistically unreachable inside ten waves, so
    // that mode offers a bigger cap rather than producing scoreless rounds.
    EXPECT_TRUE(IsWaveLimitAllowed(GameMode::Gomoku9, 10));
    EXPECT_FALSE(IsWaveLimitAllowed(GameMode::Gomoku9, 20));
    EXPECT_TRUE(IsWaveLimitAllowed(GameMode::Gomoku15, 20));
    EXPECT_FALSE(IsWaveLimitAllowed(GameMode::Gomoku15, 10));
}

TEST(GameModes, ParsesNamesAndShortForms) {
    EXPECT_EQ(ParseGameMode("tictactoe"), GameMode::TicTacToe);
    EXPECT_EQ(ParseGameMode("TicTacToe"), GameMode::TicTacToe);
    EXPECT_EQ(ParseGameMode("ttt"), GameMode::TicTacToe);
    EXPECT_EQ(ParseGameMode("gomoku9"), GameMode::Gomoku9);
    EXPECT_EQ(ParseGameMode("g9"), GameMode::Gomoku9);
    EXPECT_EQ(ParseGameMode("15"), GameMode::Gomoku15);
    EXPECT_EQ(ParseGameMode("nonsense"), GameMode::None);
}

TEST(MatchConfig, ClampsIllegalCombinationsRatherThanRejecting) {
    MatchConfig config;
    config.rounds = 4;          // only 3, 5, 7 exist
    config.ammoPerWave = 6;     // not offered for tic-tac-toe
    config.waveLimit = 99;      // not offered anywhere
    config.paperSizeMin = 9;    // out of range
    config.paperSizeMax = 0;

    EXPECT_FALSE(ClampMatchConfig(GameMode::TicTacToe, config));

    EXPECT_EQ(config.rounds, 3);
    EXPECT_TRUE(IsAmmoAllowed(GameMode::TicTacToe, config.ammoPerWave));
    EXPECT_TRUE(IsWaveLimitAllowed(GameMode::TicTacToe, config.waveLimit));
    EXPECT_GE(config.paperSizeMin, kMinPaperSize);
    EXPECT_LE(config.paperSizeMax, kMaxPaperSize);
    EXPECT_LE(config.paperSizeMin, config.paperSizeMax);
}

TEST(MatchConfig, AcceptsAValidCombinationUnchanged) {
    MatchConfig config;
    config.rounds = 5;
    config.ammoPerWave = 6;
    config.waveLimit = 10;
    config.paperSizeMin = 2;
    config.paperSizeMax = 4;
    config.itemMask = kAllItemsMask;

    EXPECT_TRUE(ClampMatchConfig(GameMode::Gomoku9, config));
    EXPECT_EQ(config.rounds, 5);
    EXPECT_EQ(config.ammoPerWave, 6);
}

TEST(MatchConfig, EveryModeHasAPlayableDefault) {
    // Quick match ships these without anyone reviewing them, so the table has
    // to be legal for its own mode by construction.
    for (const GameMode mode :
         {GameMode::TicTacToe, GameMode::Gomoku9, GameMode::Gomoku15}) {
        MatchConfig config = DefaultMatchConfig(mode);
        EXPECT_TRUE(ClampMatchConfig(mode, config))
            << "default for " << ToString(mode) << " needed clamping";
    }
}

TEST(MatchConfig, DefaultPaperSizeKeepsTheSheetSaneOnEveryBoard) {
    // Cell size depends only on the size setting, so the sheet is as wide as
    // cellSize * boardWidth. Without a per-mode band, tic-tac-toe's three-cell
    // sheet would be a fifth the width of gomoku's fifteen-cell one; this
    // pins the widths to the same rough range instead.
    struct Case {
        GameMode mode;
        uint8 cells;
    };
    constexpr Case kCases[] = {
        {GameMode::TicTacToe, 3},
        {GameMode::Gomoku9, 9},
        {GameMode::Gomoku15, 15},
    };

    for (const Case& test : kCases) {
        const MatchConfig config = DefaultMatchConfig(test.mode);
        const float widest = (24.0f + config.paperSizeMax * 8.0f) * test.cells;
        const float narrowest = (24.0f + config.paperSizeMin * 8.0f) * test.cells;

        // Big enough to aim at, and small enough to leave field to fly across.
        EXPECT_GE(narrowest, 150.0f) << ToString(test.mode) << " sheet too small";
        EXPECT_LE(widest, kFieldWidth * 0.5f) << ToString(test.mode) << " sheet too wide";
    }
}

TEST(MatchConfig, SingleShotModesLoseTheItemsThatNeedTwo) {
    // Tic-tac-toe defaults to one shot a wave, which excludes the pan and the
    // speed loader. Asserted rather than left implicit: it is a real gameplay
    // consequence of the ammo default, not an accident.
    const MatchConfig config = DefaultMatchConfig(GameMode::TicTacToe);
    ASSERT_EQ(config.ammoPerWave, 1);
    EXPECT_EQ(config.itemMask & ItemBit(ItemKind::FryingPan), 0u);
    EXPECT_EQ(config.itemMask & ItemBit(ItemKind::SpeedLoader), 0u);
    EXPECT_NE(config.itemMask & ItemBit(ItemKind::Grenade), 0u);

    // Both gomoku modes keep everything.
    for (const GameMode mode : {GameMode::Gomoku9, GameMode::Gomoku15}) {
        const MatchConfig gomoku = DefaultMatchConfig(mode);
        EXPECT_EQ(gomoku.itemMask, kAllItemsMask) << ToString(mode);
    }
}

TEST(Items, PoolNarrowsToWhatTheAmmoRuleAllows) {
    // With a single shot each, losing a wave to a frying pan is the whole
    // round, and there is nothing to reload.
    const uint32 single = EffectiveItemMask(kAllItemsMask, 1);
    EXPECT_EQ(single & ItemBit(ItemKind::FryingPan), 0u);
    EXPECT_EQ(single & ItemBit(ItemKind::SpeedLoader), 0u);
    EXPECT_NE(single & ItemBit(ItemKind::Grenade), 0u);

    // Unlimited ammo makes a reload meaningless, but the pan is fine.
    const uint32 unlimited = EffectiveItemMask(kAllItemsMask, kUnlimited);
    EXPECT_EQ(unlimited & ItemBit(ItemKind::SpeedLoader), 0u);
    EXPECT_NE(unlimited & ItemBit(ItemKind::FryingPan), 0u);

    const uint32 plenty = EffectiveItemMask(kAllItemsMask, 6);
    EXPECT_NE(plenty & ItemBit(ItemKind::SpeedLoader), 0u);
    EXPECT_NE(plenty & ItemBit(ItemKind::FryingPan), 0u);

    // Narrowing never adds anything the host had switched off.
    const uint32 onlyBalloon = EffectiveItemMask(ItemBit(ItemKind::Balloon), 6);
    EXPECT_EQ(onlyBalloon, ItemBit(ItemKind::Balloon));
}

TEST(Items, LayersPutBlockersInFrontAndDecoysBehind) {
    // The tumbleweed has to be tested before anything else, and the decoy has
    // to sit with the targets or it stops being a convincing mistake.
    EXPECT_EQ(FindItem(ItemKind::Tumbleweed)->layer, HitLayer::Blocker);
    EXPECT_EQ(FindItem(ItemKind::WantedPoster)->layer, HitLayer::Target);
    EXPECT_EQ(FindItem(ItemKind::Grenade)->layer, HitLayer::Item);
    EXPECT_EQ(FindItem(ItemKind::Balloon)->layer, HitLayer::Item);
}

TEST(TextValidation, AcceptsPlainAndMultibyteText) {
    EXPECT_TRUE(IsCleanUtf8("player", 24));
    EXPECT_TRUE(IsCleanUtf8("플레이어", 24));       // Korean, 3 bytes per char
    EXPECT_TRUE(IsCleanUtf8("emoji \xF0\x9F\x8E\xAE", 24));  // 4-byte sequence
}

TEST(TextValidation, RejectsEmptyAndOverlong) {
    EXPECT_FALSE(IsCleanUtf8("", 24));
    EXPECT_FALSE(IsCleanUtf8(std::string(25, 'x'), 24));
}

TEST(TextValidation, RejectsControlCharacters) {
    // Escape sequences and fake newlines would otherwise reach other players'
    // terminals verbatim.
    EXPECT_FALSE(IsCleanUtf8("bad\nname", 24));
    EXPECT_FALSE(IsCleanUtf8("bad\x1b[31mname", 24));
    EXPECT_FALSE(IsCleanUtf8(std::string("null\0here", 9), 24));
    EXPECT_FALSE(IsCleanUtf8("del\x7f", 24));
}

TEST(TextValidation, RejectsMalformedUtf8) {
    EXPECT_FALSE(IsCleanUtf8("\xC3", 24));              // truncated 2-byte lead
    EXPECT_FALSE(IsCleanUtf8("\xE2\x82", 24));          // truncated 3-byte lead
    EXPECT_FALSE(IsCleanUtf8("\x80\x80", 24));          // continuation as lead
    EXPECT_FALSE(IsCleanUtf8("\xC0\xAF", 24));          // overlong '/'
    EXPECT_FALSE(IsCleanUtf8("\xED\xA0\x80", 24));      // UTF-16 surrogate
    EXPECT_FALSE(IsCleanUtf8("\xF5\x80\x80\x80", 24));  // beyond U+10FFFF
}

TEST(VoicePacket, HeaderLayoutIsStable) {
    EXPECT_EQ(sizeof(VoiceHeader), 18u);
    EXPECT_EQ(kVoiceHeaderSize, 18u);
}

TEST(VoicePacket, RejectsForeignDatagrams) {
    std::array<uint8, kVoiceHeaderSize> buffer{};
    VoiceHeader header;
    header.type = VoiceMessageType::Data;
    header.sessionId = 12;
    WriteVoiceHeader(buffer.data(), header);

    VoiceHeader parsed;
    EXPECT_TRUE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));
    EXPECT_EQ(parsed.sessionId, 12u);

    // Too short.
    EXPECT_FALSE(ReadVoiceHeader(buffer.data(), 4, parsed));

    // Wrong magic — a stray packet on a shared UDP port.
    buffer[0] = 0x00;
    EXPECT_FALSE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));

    // Wrong version.
    buffer[0] = kVoiceMagic;
    buffer[1] = 99;
    EXPECT_FALSE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));
}

TEST(VoicePacket, SenderStampRewritesOnlyTheSessionId) {
    std::array<uint8, kVoiceHeaderSize + 4> buffer{};
    VoiceHeader header;
    header.type = VoiceMessageType::Data;
    header.sessionId = 1;
    header.sequence = 77;
    header.timestamp = 960;
    WriteVoiceHeader(buffer.data(), header);
    buffer[kVoiceHeaderSize] = 0xAB;

    StampVoiceSender(buffer.data(), 4242);

    VoiceHeader parsed;
    ASSERT_TRUE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));
    EXPECT_EQ(parsed.sessionId, 4242u);
    EXPECT_EQ(parsed.sequence, 77);
    EXPECT_EQ(parsed.timestamp, 960u);
    EXPECT_EQ(parsed.type, VoiceMessageType::Data);
    EXPECT_EQ(buffer[kVoiceHeaderSize], 0xAB);  // payload untouched
}
