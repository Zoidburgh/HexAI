#include "ai/mcts.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace hexuki {
namespace mcts {

// ============================================================================
// Constructor / Destructor
// ============================================================================

MCTS::MCTS()
    : root(nullptr)
    , rng(std::random_device{}()) {
}

MCTS::~MCTS() {
    resetTree();
}

void MCTS::resetTree() {
    if (root != nullptr) {
        delete root;
        root = nullptr;
    }
}

// ============================================================================
// Main Search Function
// ============================================================================

MCTSResult MCTS::findBestMove(HexukiBitboard& board, const MCTSConfig& config) {
    auto startTime = std::chrono::steady_clock::now();

    // Store root player so we can evaluate from their perspective
    rootPlayer = board.getCurrentPlayer();

    // Initialize root node
    resetTree();
    root = new MCTSNode();
    root->playerToMove = rootPlayer;  // Root player makes the first move
    root->untriedMoves = board.getValidMoves();

    MCTSResult result;
    result.simulations = 0;

    // Main MCTS loop
    while (true) {
        // Check time limit
        if (config.useTimeLimit) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            if (elapsed >= config.timeLimitMs) {
                break;
            }
        } else {
            // Check simulation count
            if (result.simulations >= config.numSimulations) {
                break;
            }
        }

        // Make a copy of the board for this simulation
        HexukiBitboard simBoard = board;

        // 1. SELECTION: Traverse tree using UCT
        MCTSNode* node = select(root, simBoard);

        // 2. EXPANSION: Add a child node if not terminal
        if (!isTerminal(simBoard) && !node->untriedMoves.empty()) {
            node = expand(node, simBoard);
        }

        // 3. SIMULATION: Play random game to end
        double score = simulate(simBoard);

        // 4. BACKPROPAGATION: Update all ancestors
        backpropagate(node, score);

        result.simulations++;

        // Print progress
        if (config.verbose && result.simulations % 1000 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            std::cout << "Simulations: " << result.simulations
                      << " | Time: " << elapsed << "ms"
                      << " | Root visits: " << root->visits << std::endl;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    result.timeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Select best move (most visited child)
    if (root->children.empty()) {
        // No children expanded - just return first untried move
        if (!root->untriedMoves.empty()) {
            result.bestMove = root->untriedMoves[0];
        }
        return result;
    }

    MCTSNode* bestChild = nullptr;
    int maxVisits = -1;

    for (MCTSNode* child : root->children) {
        if (child->visits > maxVisits) {
            maxVisits = child->visits;
            bestChild = child;
        }
    }

    if (bestChild != nullptr) {
        result.bestMove = bestChild->move;
        result.visits = bestChild->visits;
        // Invert child's score to get win rate from root player's perspective
        // Children store from opponent's perspective, we want from our perspective
        result.winRate = 1.0 - bestChild->getAverageScore();

        // Collect top moves for analysis
        std::vector<MCTSNode*> sortedChildren = root->children;
        std::sort(sortedChildren.begin(), sortedChildren.end(),
                  [](MCTSNode* a, MCTSNode* b) { return a->visits > b->visits; });

        for (size_t i = 0; i < std::min(size_t(10), sortedChildren.size()); i++) {
            MCTSResult::MoveStats stats;
            stats.move = sortedChildren[i]->move;
            stats.visits = sortedChildren[i]->visits;
            // Invert to show from root player's perspective (the player making the move)
            stats.winRate = 1.0 - sortedChildren[i]->getAverageScore();
            result.topMoves.push_back(stats);
        }
    }

    return result;
}

// Simple interfaces
MCTSResult MCTS::findBestMove(HexukiBitboard& board, int simulations) {
    MCTSConfig config;
    config.numSimulations = simulations;
    config.useTimeLimit = false;
    return findBestMove(board, config);
}

MCTSResult MCTS::findBestMoveWithTime(HexukiBitboard& board, int timeLimitMs) {
    MCTSConfig config;
    config.timeLimitMs = timeLimitMs;
    config.useTimeLimit = true;
    return findBestMove(board, config);
}

// ============================================================================
// MCTS Phases
// ============================================================================

/**
 * SELECTION PHASE
 * Traverse tree from root to leaf using UCT selection
 */
MCTSNode* MCTS::select(MCTSNode* node, HexukiBitboard& board) {
    while (!node->isLeaf() && node->isFullyExpanded()) {
        // All children have been tried, select best using UCT
        MCTSNode* bestChild = node->selectBestChild(1.414);  // √2 exploration constant
        if (bestChild == nullptr) break;

        // Make the move on the board
        board.makeMove(bestChild->move);
        node = bestChild;
    }

    return node;
}

/**
 * EXPANSION PHASE
 * Add one child node for an untried move
 */
MCTSNode* MCTS::expand(MCTSNode* node, HexukiBitboard& board) {
    if (node->untriedMoves.empty()) {
        return node;
    }

    // Pick a random untried move
    std::uniform_int_distribution<size_t> dist(0, node->untriedMoves.size() - 1);
    size_t idx = dist(rng);
    Move move = node->untriedMoves[idx];

    // Remove from untried moves
    node->untriedMoves.erase(node->untriedMoves.begin() + idx);

    // Make the move
    board.makeMove(move);

    // Create child node
    MCTSNode* child = node->addChild(move);
    child->playerToMove = board.getCurrentPlayer();  // After move, it's opponent's turn

    // Initialize child's untried moves
    if (!isTerminal(board)) {
        child->untriedMoves = board.getValidMoves();
    }

    return child;
}

/**
 * SIMULATION PHASE (ROLLOUT)
 * Play random moves until game ends
 * Returns score from Player 1's perspective
 */
double MCTS::simulate(HexukiBitboard& board) {
    // Play random moves until game over
    while (!isTerminal(board)) {
        std::vector<Move> moves = board.getValidMoves();

        if (moves.empty()) break;

        // Select random move
        Move move = selectRandomMove(moves);
        board.makeMove(move);
    }

    // Return final score from P1's perspective
    return evaluateTerminal(board);
}

/**
 * BACKPROPAGATION PHASE
 * Update all ancestor nodes with simulation result
 *
 * Score is ALWAYS from Player 1's perspective (1.0 = P1 wins, 0.0 = P2 wins)
 * Each node stores wins from ITS playerToMove's perspective
 */
void MCTS::backpropagate(MCTSNode* node, double score) {
    while (node != nullptr) {
        // Store score from this node's player perspective
        // If this is P1's node (P1 to move), use score as-is
        // If this is P2's node (P2 to move), invert (P2 wants opposite of P1)
        double nodeScore = (node->playerToMove == PLAYER_1) ? score : (1.0 - score);
        node->update(nodeScore);
        node = node->parent;
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

std::vector<Move> MCTS::getValidMoves(const HexukiBitboard& board) const {
    return board.getValidMoves();
}

bool MCTS::isTerminal(const HexukiBitboard& board) const {
    return board.isGameOver();
}

double MCTS::evaluateTerminal(const HexukiBitboard& board) const {
    // ALWAYS return from Player 1's perspective (1.0 = P1 wins, 0.0 = P2 wins)
    // This matches the JavaScript implementation
    int p1Score = board.getScore(PLAYER_1);
    int p2Score = board.getScore(PLAYER_2);

    return (p1Score > p2Score) ? 1.0 : 0.0;
}

Move MCTS::selectRandomMove(const std::vector<Move>& moves) {
    std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
    return moves[dist(rng)];
}

} // namespace mcts
} // namespace hexuki
