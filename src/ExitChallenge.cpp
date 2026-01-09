#include "ExitChallenge.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>

ExitChallenge::ExitChallenge() {
    // Seed RNG with current time
    auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    m_rng.seed(static_cast<unsigned int>(seed));
}

void ExitChallenge::configure(ChallengeType type, const std::string& customPhrase) {
    m_type = type;
    if (!customPhrase.empty()) {
        m_customPhrase = customPhrase;
    }
    FE_DEBUG("Exit challenge configured: type={}, phrase='{}'", 
             static_cast<int>(type), m_customPhrase);
}

std::string ExitChallenge::initiateChallenge() {
    m_challengeActive = true;
    m_remainingConfirms = 3;
    
    switch (m_type) {
        case ChallengeType::None:
            m_challengeActive = false;
            return "";
            
        case ChallengeType::TypePhrase: {
            m_expectedAnswer = normalizeAnswer(m_customPhrase);
            m_currentPrompt = "To stop the session, type: \"" + m_customPhrase + "\"\n"
                              "Use: hyfocus:confirm <your answer>";
            FE_INFO("TypePhrase challenge initiated");
            return m_currentPrompt;
        }
        
        case ChallengeType::MathProblem: {
            m_currentPrompt = generateMathProblem();
            FE_INFO("MathProblem challenge initiated: answer={}", m_expectedAnswer);
            return m_currentPrompt;
        }
        
        case ChallengeType::Countdown: {
            m_currentPrompt = "Are you SURE you want to stop? (" + 
                              std::to_string(m_remainingConfirms) + " confirmations needed)\n"
                              "Type: hyfocus:confirm yes";
            FE_INFO("Countdown challenge initiated");
            return m_currentPrompt;
        }
    }
    
    return "";
}

bool ExitChallenge::validateAnswer(const std::string& answer) {
    if (!m_challengeActive) {
        return true;  // No active challenge
    }
    
    std::string normalizedAnswer = normalizeAnswer(answer);
    
    switch (m_type) {
        case ChallengeType::None:
            m_challengeActive = false;
            return true;
            
        case ChallengeType::TypePhrase: {
            if (normalizedAnswer == m_expectedAnswer) {
                m_challengeActive = false;
                FE_INFO("TypePhrase challenge passed!");
                return true;
            }
            FE_DEBUG("TypePhrase wrong: got '{}', expected '{}'", 
                     normalizedAnswer, m_expectedAnswer);
            return false;
        }
        
        case ChallengeType::MathProblem: {
            if (normalizedAnswer == m_expectedAnswer) {
                m_challengeActive = false;
                FE_INFO("MathProblem challenge passed!");
                return true;
            }
            FE_DEBUG("MathProblem wrong: got '{}', expected '{}'", 
                     normalizedAnswer, m_expectedAnswer);
            return false;
        }
        
        case ChallengeType::Countdown: {
            if (normalizedAnswer == "yes" || normalizedAnswer == "y") {
                m_remainingConfirms--;
                if (m_remainingConfirms <= 0) {
                    m_challengeActive = false;
                    FE_INFO("Countdown challenge passed!");
                    return true;
                }
                // Update prompt for next confirmation
                m_currentPrompt = "Still sure? (" + 
                                  std::to_string(m_remainingConfirms) + " more confirmations needed)\n"
                                  "Type: hyfocus:confirm yes";
                FE_DEBUG("Countdown: {} remaining", m_remainingConfirms);
            }
            return false;  // Need more confirmations
        }
    }
    
    return false;
}

void ExitChallenge::cancelChallenge() {
    m_challengeActive = false;
    m_remainingConfirms = 3;
    FE_DEBUG("Challenge cancelled");
}

std::string ExitChallenge::getHint() const {
    switch (m_type) {
        case ChallengeType::TypePhrase:
            return "Hint: Type the exact phrase shown (case-insensitive)";
        case ChallengeType::MathProblem:
            return "Hint: Calculate the answer and submit just the number";
        case ChallengeType::Countdown:
            return "Hint: Keep typing 'yes' to confirm";
        default:
            return "";
    }
}

std::string ExitChallenge::generateMathProblem() {
    std::uniform_int_distribution<int> opDist(0, 2);
    std::uniform_int_distribution<int> numDist(10, 50);
    
    int a = numDist(m_rng);
    int b = numDist(m_rng);
    int op = opDist(m_rng);
    int result;
    std::string opStr;
    
    switch (op) {
        case 0:  // Addition
            result = a + b;
            opStr = "+";
            break;
        case 1:  // Subtraction (ensure positive result)
            if (a < b) std::swap(a, b);
            result = a - b;
            opStr = "-";
            break;
        case 2:  // Multiplication (use smaller numbers)
            a = a % 13 + 2;  // 2-14
            b = b % 13 + 2;
            result = a * b;
            opStr = "×";
            break;
        default:
            result = a + b;
            opStr = "+";
    }
    
    m_expectedAnswer = std::to_string(result);
    
    return "Solve to stop: " + std::to_string(a) + " " + opStr + " " + std::to_string(b) + " = ?\n"
           "Use: hyfocus:confirm <answer>";
}

std::string ExitChallenge::normalizeAnswer(const std::string& input) const {
    std::string result;
    result.reserve(input.size());
    
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            result += std::tolower(static_cast<unsigned char>(c));
        }
    }
    
    // Trim leading/trailing spaces that might have been in original
    size_t start = result.find_first_not_of(' ');
    size_t end = result.find_last_not_of(' ');
    
    if (start == std::string::npos) {
        return "";
    }
    
    return result.substr(start, end - start + 1);
}
