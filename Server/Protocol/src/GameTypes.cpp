#include "protocol/GameTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace nhn::proto {
namespace {

// Layer is declared once, here. The world stores each item in the container its
// layer names, so hit-test order comes from the code's structure rather than
// from a number that could be typo'd.
//
//                                layer            minAmmo  banned on unlimited
constexpr std::array<ItemDef, 7> kItems = {{
    {ItemKind::Tumbleweed,   HitLayer::Blocker, 0, false, "tumbleweed"},
    {ItemKind::WantedPoster, HitLayer::Target,  0, false, "poster"},
    {ItemKind::FryingPan,    HitLayer::Item,    2, false, "pan"},
    {ItemKind::Balloon,      HitLayer::Item,    0, false, "balloon"},
    {ItemKind::PaintCan,     HitLayer::Item,    0, false, "paint"},
    {ItemKind::Grenade,      HitLayer::Item,    0, false, "grenade"},
    {ItemKind::SpeedLoader,  HitLayer::Item,    2, true,  "speedloader"},
}};

constexpr std::array<uint8, 3> kRoundOptions = {3, 5, 7};

}  // namespace

const ItemDef* FindItem(ItemKind kind) {
    for (const ItemDef& def : kItems) {
        if (def.kind == kind) {
            return &def;
        }
    }
    return nullptr;
}

const char* ToString(ItemKind kind) {
    const ItemDef* def = FindItem(kind);
    return def != nullptr ? def->name : "none";
}

ItemKind ParseItemKind(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const ItemDef& def : kItems) {
        if (lowered == def.name) {
            return def.kind;
        }
    }
    return ItemKind::None;
}

uint32 EffectiveItemMask(uint32 requestedMask, uint8 ammoPerWave) {
    uint32 result = 0;
    for (const ItemDef& def : kItems) {
        if ((requestedMask & ItemBit(def.kind)) == 0) {
            continue;
        }
        if (ammoPerWave == kUnlimited) {
            if (def.bannedOnUnlimitedAmmo) {
                continue;
            }
        } else if (ammoPerWave < def.minAmmo) {
            continue;
        }
        result |= ItemBit(def.kind);
    }
    return result;
}

bool ClampMatchConfig(GameMode mode, MatchConfig& config) {
    const GameModeDef* info = FindGameMode(mode);
    if (info == nullptr) {
        return false;
    }

    const MatchConfig original = config;

    if (std::find(kRoundOptions.begin(), kRoundOptions.end(), config.rounds) ==
        kRoundOptions.end()) {
        config.rounds = 3;
    }

    config.paperSizeMin = std::clamp(config.paperSizeMin, kMinPaperSize, kMaxPaperSize);
    config.paperSizeMax = std::clamp(config.paperSizeMax, kMinPaperSize, kMaxPaperSize);
    if (config.paperSizeMin > config.paperSizeMax) {
        std::swap(config.paperSizeMin, config.paperSizeMax);
    }

    if (!IsAmmoAllowed(mode, config.ammoPerWave)) {
        config.ammoPerWave = info->ammoOptions[0];
    }
    if (!IsWaveLimitAllowed(mode, config.waveLimit)) {
        config.waveLimit = info->waveLimitOptions[0];
    }

    // Stored narrowed, so every later reader — spawner, lobby UI, handoff —
    // sees the same set without having to re-derive it.
    config.itemMask = EffectiveItemMask(config.itemMask, config.ammoPerWave);

    return config.rounds == original.rounds && config.paperSizeMin == original.paperSizeMin &&
           config.paperSizeMax == original.paperSizeMax &&
           config.ammoPerWave == original.ammoPerWave && config.waveLimit == original.waveLimit &&
           config.itemMask == original.itemMask;
}

}  // namespace nhn::proto
