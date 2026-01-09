// ExitChallenge - optional minigame before stopping a session
#pragma once

#include "globals.hpp"
#include <random>
#include <string>

enum class ChallengeType {
    None,        // no challenge
    TypePhrase,  // type a phrase
    MathProblem, // solve math
    Countdown    // confirm 3 times
};

class ExitChallenge {
public:
    ExitChallenge();
    ~ExitChallenge() = default;

    void configure(ChallengeType type, const std::string& customPhrase = "");
    void setChallengeType(ChallengeType type) { m_type = type; }
    ChallengeType getChallengeType() const { return m_type; }
    bool isEnabled() const { return m_type != ChallengeType::None; }

    // Start a challenge, returns instructions for user
    std::string initiateChallenge();
    bool isChallengeActive() const { return m_challengeActive; }
    
    // Check answer, returns true if correct
    bool validateAnswer(const std::string& answer);
    void cancelChallenge();
    
    std::string getHint() const;
    int getRemainingAttempts() const { return m_remainingConfirms; }

private:
    std::string generateMathProblem();
    std::string normalizeAnswer(const std::string& input) const;

    ChallengeType m_type{ChallengeType::None};
    std::string m_customPhrase{"I want to stop focusing"};
    
    bool m_challengeActive{false};
    std::string m_expectedAnswer;
    std::string m_currentPrompt;
    int m_remainingConfirms{3};
    
    std::mt19937 m_rng;
};
