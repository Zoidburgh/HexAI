#include "core/zobrist.h"
#include "core/bitboard.h"
#include "utils/constants.h"
#include <random>

namespace hexuki {

// Static member initialization
uint64_t Zobrist::tileHashes[NUM_HEXES][MAX_TILE_VALUE + 1] = {};
uint64_t Zobrist::playerHashes[2] = {};
bool Zobrist::initialized = false;

void Zobrist::initialize() {
    if (initialized) return;

    // Use fixed seed for reproducibility (same hashes across runs)
    std::mt19937_64 rng(0x1234567890ABCDEF);
    std::uniform_int_distribution<uint64_t> dist;

    // Generate random hashes for tile placements
    // Only generate for valid tile values from TILE_VALUES array
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        for (int i = 0; i < NUM_TILES_PER_PLAYER; i++) {
            int tileVal = TILE_VALUES[i];
            tileHashes[hexId][tileVal] = dist(rng);
        }
    }

    // Generate random hashes for player-to-move
    for (int player = 0; player < 2; player++) {
        playerHashes[player] = dist(rng);
    }

    initialized = true;
}

uint64_t Zobrist::getTileHash(int hexId, int tileValue) {
    if (!initialized) initialize();
    return tileHashes[hexId][tileValue];  // Direct indexing by tile value
}

uint64_t Zobrist::getPlayerHash(int player) {
    if (!initialized) initialize();
    return playerHashes[player - 1];  // player is 1-2, array is 0-1
}

uint64_t Zobrist::hash(const HexukiBitboard& board) {
    if (!initialized) initialize();

    uint64_t h = 0;

    // XOR in all tile placements (ONE tile per hex)
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        int tileVal = board.getTileValue(hexId);
        if (tileVal > 0) {
            h ^= getTileHash(hexId, tileVal);
        }
    }

    // XOR in player-to-move
    h ^= getPlayerHash(board.getCurrentPlayer());

    return h;
}

} // namespace hexuki
