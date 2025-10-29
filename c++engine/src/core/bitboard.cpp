#include "core/bitboard.h"
#include "core/zobrist.h"
#include "utils/timer.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <set>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace hexuki {

// ============================================================================
// Helper Functions
// ============================================================================

// Check if two tile vectors are identical (same values in any order)
static bool tilesMatch(std::vector<int> tiles1, std::vector<int> tiles2) {
    if (tiles1.size() != tiles2.size()) return false;
    std::sort(tiles1.begin(), tiles1.end());
    std::sort(tiles2.begin(), tiles2.end());
    return tiles1 == tiles2;
}

// ============================================================================
// Constructor & Reset
// ============================================================================

HexukiBitboard::HexukiBitboard()
    : hexOccupied(0)
    , hexValues{}
    , p1AvailableTiles(ALL_TILES_MASK)  // All tiles 1-NUM_TILE_VALUES available
    , p2AvailableTiles(ALL_TILES_MASK)
    , currentPlayer(PLAYER_1)
    , symmetryStillPossible(true)
    , tilesAreIdentical(true)
    , zobristHash(0)
{
    reset();
}

void HexukiBitboard::reset() {
    // Clear board
    hexOccupied = 0;
    std::memset(hexValues, 0, sizeof(hexValues));

    // Reset available tiles (all tiles 1-9 available)
    // Array-based: supports standard [1,2,3,4,5,6,7,8,9] and asymmetric sets
    p1AvailableTiles = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    p2AvailableTiles = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    // Initial state: center hex (9) has tile value 1
    hexOccupied = (1u << CENTER_HEX);
    hexValues[CENTER_HEX] = STARTING_TILE;

    currentPlayer = PLAYER_1;
    symmetryStillPossible = true;
    tilesAreIdentical = tilesMatch(p1AvailableTiles, p2AvailableTiles);

    zobristHash = Zobrist::hash(*this);
}

// ============================================================================
// State Queries
// ============================================================================

bool HexukiBitboard::isHexOccupied(int hexId) const {
    if (hexId < 0 || hexId >= NUM_HEXES) return false;
    return (hexOccupied & (1u << hexId)) != 0;
}

int HexukiBitboard::getTileValue(int hexId) const {
    if (hexId < 0 || hexId >= NUM_HEXES) return 0;
    return hexValues[hexId];
}

bool HexukiBitboard::isGameOver() const {
    // Game ends when all 19 hexes are filled
    // Can't use moveCount >= 18 because puzzles might have empty center hex (allowing 19 moves)
    int occupiedCount = 0;
    for (int i = 0; i < NUM_HEXES; i++) {
        if (isHexOccupied(i)) occupiedCount++;
    }
    return occupiedCount >= NUM_HEXES;
}

bool HexukiBitboard::isTileAvailable(int player, int tileValue) const {
    if (tileValue < 1 || tileValue > MAX_TILE_VALUE) return false;
    const std::vector<int>& tiles = (player == PLAYER_1) ? p1AvailableTiles : p2AvailableTiles;
    // Use std::find to check if tile value exists in array (supports duplicates)
    return std::find(tiles.begin(), tiles.end(), tileValue) != tiles.end();
}

std::vector<int> HexukiBitboard::getAvailableTiles(int player) const {
    // Simply return the tile array (already supports duplicates)
    return (player == PLAYER_1) ? p1AvailableTiles : p2AvailableTiles;
}

// ============================================================================
// Adjacency (REAL hex grid adjacency)
// ============================================================================

int HexukiBitboard::findHexAt(int row, int col) const {
    for (int i = 0; i < NUM_HEXES; i++) {
        if (HEX_POSITIONS[i].row == row && HEX_POSITIONS[i].col == col) {
            return i;
        }
    }
    return -1;  // Not found
}

std::vector<int> HexukiBitboard::getAdjacentHexes(int hexId) const {
    std::vector<int> adjacent;
    if (hexId < 0 || hexId >= NUM_HEXES) return adjacent;

    const HexPosition& hex = HEX_POSITIONS[hexId];

    // Check all 6 hex directions
    for (int i = 0; i < 6; i++) {
        int newRow = hex.row + HEX_DIRECTIONS[i].dr;
        int newCol = hex.col + HEX_DIRECTIONS[i].dc;

        int adjId = findHexAt(newRow, newCol);
        if (adjId >= 0) {
            adjacent.push_back(adjId);
        }
    }

    return adjacent;
}

bool HexukiBitboard::hasAdjacentOccupied(int hexId) const {
    auto adjacent = getAdjacentHexes(hexId);
    for (int adjId : adjacent) {
        if (isHexOccupied(adjId)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Chain Length Constraint (REAL algorithm from JavaScript)
// ============================================================================

std::vector<int> HexukiBitboard::getChainLengthsFromStart(int startHex, const Direction& dir) const {
    std::vector<int> lengths;
    int currentLength = 0;
    int currentHex = startHex;

    while (currentHex >= 0) {
        if (isHexOccupied(currentHex)) {
            currentLength++;
        } else if (currentLength > 0) {
            // Hit empty cell, record current chain and reset
            lengths.push_back(currentLength);
            currentLength = 0;
        }

        // Move to next cell in direction
        const HexPosition& pos = HEX_POSITIONS[currentHex];
        int newRow = pos.row + dir.dr;
        int newCol = pos.col + dir.dc;
        currentHex = findHexAt(newRow, newCol);
    }

    // Record final chain if we ended on occupied cells
    if (currentLength > 0) {
        lengths.push_back(currentLength);
    }

    return lengths;
}

std::vector<int> HexukiBitboard::getAllChainLengths() const {
    std::vector<int> chainLengths;

    // Check all chain starters
    for (int i = 0; i < 15; i++) {
        const ChainStarter& starter = CHAIN_STARTERS[i];
        auto lengths = getChainLengthsFromStart(starter.startHex, starter.dir);
        chainLengths.insert(chainLengths.end(), lengths.begin(), lengths.end());
    }

    return chainLengths;
}

std::vector<HexukiBitboard::ChainInfo> HexukiBitboard::getAllChainsWithMembers() const {
    std::vector<ChainInfo> chains;

    // Check all chain starters
    for (int i = 0; i < 15; i++) {
        const ChainStarter& starter = CHAIN_STARTERS[i];
        std::vector<int> currentChain;
        int currentHex = starter.startHex;

        while (currentHex >= 0) {
            if (isHexOccupied(currentHex)) {
                currentChain.push_back(currentHex);
            } else if (!currentChain.empty()) {
                // Hit empty cell, record current chain and reset
                ChainInfo info;
                info.length = currentChain.size();
                info.hexIds = currentChain;
                chains.push_back(info);
                currentChain.clear();
            }

            // Move to next cell in direction
            const HexPosition& pos = HEX_POSITIONS[currentHex];
            int newRow = pos.row + starter.dir.dr;
            int newCol = pos.col + starter.dir.dc;
            currentHex = findHexAt(newRow, newCol);
        }

        // Record final chain if we ended on occupied cells
        if (!currentChain.empty()) {
            ChainInfo info;
            info.length = currentChain.size();
            info.hexIds = currentChain;
            chains.push_back(info);
        }
    }

    // Find isolated tiles (occupied hexes not part of any detected chain)
    std::set<int> hexesInChains;
    for (const auto& chain : chains) {
        for (int hexId : chain.hexIds) {
            hexesInChains.insert(hexId);
        }
    }

    // Add isolated tiles as 1-chains
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        if (isHexOccupied(hexId) && hexesInChains.find(hexId) == hexesInChains.end()) {
            ChainInfo info;
            info.length = 1;
            info.hexIds = {hexId};
            chains.push_back(info);
        }
    }

    return chains;
}

void HexukiBitboard::getFirstAndSecondChainLengths(int& first, int& second) const {
    auto allLengths = getAllChainLengths();
    first = 0;
    second = 0;

    for (int length : allLengths) {
        if (length > first) {
            second = first;
            first = length;
        } else if (length > second) {
            second = length;
        }
    }
}

bool HexukiBitboard::checkChainLengthConstraint(int hexId) const {
    // Make a test board with the proposed move
    HexukiBitboard testBoard = *this;
    testBoard.hexOccupied |= (1u << hexId);
    testBoard.hexValues[hexId] = 1;  // Use dummy value for testing

    // Get all chains with their member hexes
    auto allChains = testBoard.getAllChainsWithMembers();

    // Find chains that contain the newly placed hexId (affected chains)
    int longestAffected = 0;
    for (const auto& chain : allChains) {
        bool containsNewHex = std::find(chain.hexIds.begin(), chain.hexIds.end(), hexId) != chain.hexIds.end();
        if (containsNewHex && chain.length > longestAffected) {
            longestAffected = chain.length;
        }
    }

    // Get all chain lengths and sort descending
    std::vector<int> allLengths;
    for (const auto& chain : allChains) {
        allLengths.push_back(chain.length);
    }
    std::sort(allLengths.begin(), allLengths.end(), std::greater<int>());

    // Get second longest chain on entire board
    int secondLongest = allLengths.size() >= 2 ? allLengths[1] : 0;

    // Rule: longest affected chain can be at most 1 longer than second longest
    if (longestAffected > secondLongest + 1) {
        return false;
    }

    return true;
}

// ============================================================================
// Anti-Symmetry Rule (REAL algorithm from JavaScript)
// ============================================================================

bool HexukiBitboard::isBoardMirrored() const {
    // If symmetry already broken, no need to check
    if (!symmetryStillPossible) {
        return false;
    }

    // Check if board is currently symmetric
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        // Skip center column hexes (they mirror to themselves)
        bool isCenterHex = false;
        for (int i = 0; i < 5; i++) {
            if (hexId == CENTER_COLUMN_HEXES[i]) {
                isCenterHex = true;
                break;
            }
        }
        if (isCenterHex) continue;

        int mirrorHexId = VERTICAL_MIRROR_PAIRS[hexId];
        int val1 = hexValues[hexId];
        int val2 = hexValues[mirrorHexId];

        // If one is empty and the other isn't, not currently symmetric
        if ((val1 == 0) != (val2 == 0)) {
            return false;
        }

        // If both occupied but different values, not symmetric AND never will be
        if (val1 != 0 && val2 != 0 && val1 != val2) {
            // NOTE: We can't modify symmetryStillPossible here (const function)
            // This will be handled in the non-const version
            return false;
        }
    }

    return true;  // Board is currently symmetric
}

// ============================================================================
// Move Validation (REAL rules from JavaScript)
// ============================================================================

bool HexukiBitboard::isMoveLegal(int hexId) const {
    // Check if hex is empty
    if (isHexOccupied(hexId)) {
        return false;
    }

    // Check if adjacent to an occupied hex
    if (!hasAdjacentOccupied(hexId)) {
        return false;
    }

    // Check chain length constraint
    if (!checkChainLengthConstraint(hexId)) {
        return false;
    }

    return true;
}

bool HexukiBitboard::isValidMove(const Move& move) const {
    if (!move.isValid()) return false;

    // Check if position is legal
    if (!isMoveLegal(move.hexId)) {
        return false;
    }

    // Check if tile is available
    if (!isTileAvailable(currentPlayer, move.tileValue)) {
        return false;
    }

    // Symmetry checks removed - no longer enforcing anti-symmetry rule

    return true;
}

std::vector<Move> HexukiBitboard::getValidMoves() const {
    std::vector<Move> moves;
    auto availableTiles = getAvailableTiles(currentPlayer);

    // Get unique tile values (handle duplicates like [1,1,1,1,1,1,1,1,1])
    // If tiles = [1,1,1], we only want to try placing "1" once, not three times
    std::vector<int> uniqueTileValues = availableTiles;
    std::sort(uniqueTileValues.begin(), uniqueTileValues.end());
    uniqueTileValues.erase(std::unique(uniqueTileValues.begin(), uniqueTileValues.end()), uniqueTileValues.end());

    // Symmetry checks removed - no longer enforcing anti-symmetry rule

    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        if (isHexOccupied(hexId)) continue;

        if (isMoveLegal(hexId)) {
            // Try each unique tile value (avoids generating duplicate moves)
            for (int tileValue : uniqueTileValues) {
                // Add all valid moves without symmetry checks
                moves.push_back(Move(hexId, tileValue));
            }
        }
    }

    return moves;
}

// ============================================================================
// Move Execution
// ============================================================================

void HexukiBitboard::makeMove(const Move& move) {
    // Place tile on board
    hexOccupied |= (1u << move.hexId);
    hexValues[move.hexId] = move.tileValue;

    // Remove tile from current player's available tiles
    std::vector<int>& tiles = (currentPlayer == PLAYER_1) ? p1AvailableTiles : p2AvailableTiles;
    auto it = std::find(tiles.begin(), tiles.end(), move.tileValue);
    if (it != tiles.end()) {
        tiles.erase(it);  // Remove first occurrence
    }

    // Update symmetry tracking (if we ever re-enable it)
    if (symmetryStillPossible) {
        int mirrorHexId = VERTICAL_MIRROR_PAIRS[move.hexId];
        if (hexValues[mirrorHexId] != 0 && hexValues[mirrorHexId] != move.tileValue) {
            symmetryStillPossible = false;
        }
    }

    // Update zobrist hash
    updateZobristHash(move);

    // Switch to next player
    currentPlayer = (currentPlayer == PLAYER_1) ? PLAYER_2 : PLAYER_1;
}

void HexukiBitboard::unmakeMove(const Move& move) {
    // Switch player back (undo the player switch from makeMove)
    currentPlayer = (currentPlayer == PLAYER_1) ? PLAYER_2 : PLAYER_1;

    // Reverse zobrist hash update (XOR is self-inverse)
    updateZobristHash(move);

    // Add tile back to player's available tiles
    std::vector<int>& tiles = (currentPlayer == PLAYER_1) ? p1AvailableTiles : p2AvailableTiles;
    tiles.push_back(move.tileValue);

    // Clear tile from board
    hexOccupied &= ~(1u << move.hexId);
    hexValues[move.hexId] = 0;

    // Note: symmetryStillPossible is not restored since symmetry checks are disabled
    // If symmetry is re-enabled later, this would need to track the previous state
}

// ============================================================================
// Scoring (REAL chain-based multiplication)
// ============================================================================

int HexukiBitboard::calculateChainScore(const int* chain, int chainLength) const {
    int product = 1;

    for (int i = 0; i < chainLength; i++) {
        int hexId = chain[i];
        if (hexId < 0) break;  // -1 padding in chain array

        if (isHexOccupied(hexId)) {
            product *= hexValues[hexId];
        }
    }

    return product;
}

int HexukiBitboard::calculatePlayerScore(int player) const {
    int totalScore = 0;

    if (player == PLAYER_1) {
        // P1 chains: down-right diagonals
        for (int i = 0; i < P1_CHAIN_COUNT; i++) {
            int chainScore = calculateChainScore(P1_CHAINS[i], P1_CHAIN_LENGTHS[i]);
            totalScore += chainScore;
        }
    } else {
        // P2 chains: down-left diagonals
        for (int i = 0; i < P2_CHAIN_COUNT; i++) {
            int chainScore = calculateChainScore(P2_CHAINS[i], P2_CHAIN_LENGTHS[i]);
            totalScore += chainScore;
        }
    }

    return totalScore;
}

int HexukiBitboard::getScore(int player) const {
    return calculatePlayerScore(player);
}

// ============================================================================
// Zobrist Hashing
// ============================================================================

void HexukiBitboard::updateZobristHash(const Move& move) {
    // XOR in the hash for this tile placement
    zobristHash ^= Zobrist::getTileHash(move.hexId, move.tileValue);

    // XOR in player-to-move hash
    zobristHash ^= Zobrist::getPlayerHash(currentPlayer);
}

// ============================================================================
// Debug & Utility
// ============================================================================

void HexukiBitboard::print() const {
    // Count occupied hexes for move count
    int occupiedCount = 0;
    for (int i = 0; i < NUM_HEXES; i++) {
        if (isHexOccupied(i)) occupiedCount++;
    }

    std::cout << "=== Hexuki Board State ===" << std::endl;
    std::cout << "Occupied: " << occupiedCount << "/" << NUM_HEXES << ", Player: P" << currentPlayer << std::endl;
    std::cout << "Scores: P1=" << getScore(PLAYER_1) << ", P2=" << getScore(PLAYER_2) << std::endl;
    std::cout << std::endl;

    std::cout << "Occupied hexes:" << std::endl;
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        if (!isHexOccupied(hexId)) continue;

        std::cout << "  Hex " << hexId << " (row=" << HEX_POSITIONS[hexId].row
                  << ", col=" << HEX_POSITIONS[hexId].col << "): "
                  << "value=" << static_cast<int>(hexValues[hexId]) << std::endl;
    }
    std::cout << std::endl;

    std::cout << "Available tiles:" << std::endl;
    auto p1Tiles = getAvailableTiles(PLAYER_1);
    auto p2Tiles = getAvailableTiles(PLAYER_2);

    std::cout << "  P1: [";
    for (size_t i = 0; i < p1Tiles.size(); i++) {
        std::cout << p1Tiles[i];
        if (i < p1Tiles.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;

    std::cout << "  P2: [";
    for (size_t i = 0; i < p2Tiles.size(); i++) {
        std::cout << p2Tiles[i];
        if (i < p2Tiles.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;

    std::cout << "=========================" << std::endl;
}

std::string HexukiBitboard::toNotation() const {
    // Note: Move history is no longer tracked for performance reasons
    // This function returns empty string. Use savePosition() for current state.
    return "";
}

// ============================================================================
// Puzzle Setup
// ============================================================================

void HexukiBitboard::setHexValue(int hexId, int tileValue) {
    if (hexId < 0 || hexId >= NUM_HEXES) return;

    // Place the tile
    hexOccupied |= (1u << hexId);
    hexValues[hexId] = tileValue;

    // Recalculate hash
    zobristHash = Zobrist::hash(*this);
}

void HexukiBitboard::removeHexValue(int hexId) {
    if (hexId < 0 || hexId >= NUM_HEXES) return;

    // Remove the tile
    hexOccupied &= ~(1u << hexId);
    hexValues[hexId] = 0;

    // Recalculate hash
    zobristHash = Zobrist::hash(*this);
}

void HexukiBitboard::setAvailableTiles(int player, const std::vector<int>& tiles) {
    // Directly assign tile vector (supports duplicates like [1,1,1,1,1,1,1,1,1])
    if (player == PLAYER_1) {
        p1AvailableTiles = tiles;
    } else if (player == PLAYER_2) {
        p2AvailableTiles = tiles;
    }
}

void HexukiBitboard::clearBoard() {
    // Clear all tiles but keep player and move count
    hexOccupied = 0;
    std::memset(hexValues, 0, sizeof(hexValues));
    zobristHash = Zobrist::hash(*this);
}

void HexukiBitboard::loadPosition(const std::string& position) {
    // Clear everything first
    clearBoard();
    p1AvailableTiles = {1, 2, 3, 4, 5, 6, 7, 8, 9};  // Default: all tiles 1-9 available
    p2AvailableTiles = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    currentPlayer = PLAYER_1;

    // Parse format: "h0:1,h4:5,h9:1|p1:2,3,4|p2:6,7,8|turn:1"
    std::istringstream iss(position);
    std::string section;

    bool p1Specified = false;
    bool p2Specified = false;

    while (std::getline(iss, section, '|')) {
        if (section.empty()) continue;

        // Parse hex placements: h0:1,h4:5
        if (section[0] == 'h') {
            std::istringstream hexStream(section);
            std::string hexPair;
            while (std::getline(hexStream, hexPair, ',')) {
                if (hexPair.size() < 4) continue;

                // Parse "h6:5" → hex 6, tile 5
                size_t colonPos = hexPair.find(':');
                if (colonPos == std::string::npos) continue;

                int hexId = std::stoi(hexPair.substr(1, colonPos - 1));
                int tileVal = std::stoi(hexPair.substr(colonPos + 1));

                setHexValue(hexId, tileVal);
            }
        }
        // Parse player 1 tiles: p1:2,3,4
        else if (section.substr(0, 3) == "p1:") {
            p1Specified = true;
            std::istringstream tileStream(section.substr(3));
            std::vector<int> tiles;
            std::string tileStr;
            while (std::getline(tileStream, tileStr, ',')) {
                tiles.push_back(std::stoi(tileStr));
            }
            setAvailableTiles(PLAYER_1, tiles);
        }
        // Parse player 2 tiles: p2:6,7,8
        else if (section.substr(0, 3) == "p2:") {
            p2Specified = true;
            std::istringstream tileStream(section.substr(3));
            std::vector<int> tiles;
            std::string tileStr;
            while (std::getline(tileStream, tileStr, ',')) {
                tiles.push_back(std::stoi(tileStr));
            }
            setAvailableTiles(PLAYER_2, tiles);
        }
        // Parse turn: turn:1
        else if (section.substr(0, 5) == "turn:") {
            currentPlayer = std::stoi(section.substr(5));
        }
    }

    // Recalculate symmetryStillPossible based on current board state
    // Symmetry is impossible if any mirror pair has different values
    symmetryStillPossible = true;
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        // Skip center column hexes (they mirror to themselves)
        bool isCenterHex = false;
        for (int i = 0; i < 5; i++) {
            if (hexId == CENTER_COLUMN_HEXES[i]) {
                isCenterHex = true;
                break;
            }
        }
        if (isCenterHex) continue;

        int mirrorHexId = VERTICAL_MIRROR_PAIRS[hexId];
        int val1 = hexValues[hexId];
        int val2 = hexValues[mirrorHexId];

        // If both occupied but different values, symmetry is impossible
        if (val1 != 0 && val2 != 0 && val1 != val2) {
            symmetryStillPossible = false;
            break;
        }
    }

    // Check if both players have identical starting tiles
    tilesAreIdentical = tilesMatch(p1AvailableTiles, p2AvailableTiles);

    // Recalculate hash
    zobristHash = Zobrist::hash(*this);
}

std::string HexukiBitboard::savePosition() const {
    std::ostringstream oss;

    // Save hex placements
    bool firstHex = true;
    for (int hexId = 0; hexId < NUM_HEXES; hexId++) {
        if (isHexOccupied(hexId)) {
            if (!firstHex) oss << ",";
            oss << "h" << hexId << ":" << static_cast<int>(hexValues[hexId]);
            firstHex = false;
        }
    }

    // Save player 1 available tiles
    oss << "|p1:";
    auto p1Tiles = getAvailableTiles(PLAYER_1);
    for (size_t i = 0; i < p1Tiles.size(); i++) {
        if (i > 0) oss << ",";
        oss << p1Tiles[i];
    }

    // Save player 2 available tiles
    oss << "|p2:";
    auto p2Tiles = getAvailableTiles(PLAYER_2);
    for (size_t i = 0; i < p2Tiles.size(); i++) {
        if (i > 0) oss << ",";
        oss << p2Tiles[i];
    }

    // Save current player
    oss << "|turn:" << currentPlayer;

    return oss.str();
}

} // namespace hexuki
