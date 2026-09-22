import math
from typing import Dict, List, Tuple, Union

class MCQPredictor:
    def __init__(self, total_questions: int = 10, options: List[str] = None):
        if options is None:
            options = ['A', 'B', 'C', 'D']
        self.N = total_questions
        self.options = options
        self.num_options = len(options)
        self.target_per_option = self.N / self.num_options
        
        # Base Human Transition Matrix: Low probability for self-repeats
        # Row = Prev Answer, Col = Next Answer
        self.repeat_penalty = 0.18
        self.diff_prob = (1.0 - self.repeat_penalty) / (self.num_options - 1)
        
    def _parse_clues(self, clues: Union[Dict[int, str], Tuple[Tuple[int, str], ...]]) -> Dict[int, str]:
        """Normalizes tuple or dict inputs into 1-based index dictionary."""
        if isinstance(clues, tuple):
            return {q: ans.upper() for q, ans in clues}
        return {q: ans.upper() for q, ans in clues}

    def _get_global_deficit_weights(self, known_clues: Dict[int, str]) -> Dict[str, float]:
        """Calculates global quota deficit priors based on total expected frequencies."""
        counts = {opt: 0 for opt in self.options}
        for ans in known_clues.values():
            if ans in counts:
                counts[ans] += 1
                
        # Deficit calculation: max(0, target - observed)
        deficits = {opt: max(0.1, self.target_per_option - counts[opt]) for opt in self.options}
        total_deficit = sum(deficits.values())
        
        # Convert to prior probability distribution
        return {opt: deficits[opt] / total_deficit for opt in self.options}

    def _get_transition_likelihood(self, prev_ans: str, next_ans: str) -> float:
        """Markov transition probability between two adjacent options."""
        if prev_ans == next_ans:
            return self.repeat_penalty
        return self.diff_prob

    def predict(self, clues: Union[Dict[int, str], Tuple[Tuple[int, str], ...]]) -> Dict[str, any]:
        known_clues = self.parse_clues(clues) if hasattr(self, 'parse_clues') else self._parse_clues(clues)
        deficit_priors = self._get_global_deficit_weights(known_clues)
        
        predictions = {}
        predicted_sequence = {}

        for q in range(1, self.N + 1):
            if q in known_clues:
                # 100% certainty for known clues
                ans = known_clues[q]
                predicted_sequence[q] = ans
                predictions[q] = {
                    "best_guess": ans,
                    "probabilities": {opt: (1.0 if opt == ans else 0.0) for opt in self.options},
                    "status": "KNOWN"
                }
            else:
                raw_scores = {opt: deficit_priors[opt] for opt in self.options}
                
                # --- Progressive / Sequential Influence (Left Context) ---
                if (q - 1) in known_clues:
                    prev_ans = known_clues[q - 1]
                    # Check for streak length behind q
                    streak_length = 1
                    chk = q - 2
                    while chk in known_clues and known_clues[chk] == prev_ans:
                        streak_length += 1
                        chk -= 1
                    
                    for opt in self.options:
                        lik = self._get_transition_likelihood(prev_ans, opt)
                        # Heavy penalty exponential decay for 3+ streaks
                        if opt == prev_ans and streak_length >= 2:
                            lik *= (0.2 ** (streak_length - 1))
                        raw_scores[opt] *= (1.5 * lik)
                
                # --- Progressive / Sequential Influence (Right Context) ---
                if (q + 1) in known_clues:
                    next_ans = known_clues[q + 1]
                    for opt in self.options:
                        lik = self._get_transition_likelihood(opt, next_ans)
                        raw_scores[opt] *= (1.5 * lik)

                # Normalize probabilities (Softmax/Bayesian update)
                total_score = sum(raw_scores.values())
                probs = {opt: round(raw_scores[opt] / total_score, 4) for opt in self.options}
                
                best_opt = max(probs, key=probs.get)
                predicted_sequence[q] = best_opt
                
                predictions[q] = {
                    "best_guess": best_opt,
                    "probabilities": probs,
                    "status": "PREDICTED"
                }

        # Build clean string sequence
        full_seq = [predicted_sequence[i] for i in range(1, self.N + 1)]
        
        return {
            "predictions": predictions,
            "full_predicted_sequence": full_seq
        }

# ==========================================
# Example Usage with Non-Progressive Clues
# ==========================================
if __name__ == "__main__":
    predictor = MCQPredictor(total_questions=10)

    # Input format 1: Tuple of (Question_Number, Correct_Answer)
    # Notice these can be sparse / non-progressive
    clues_tuple = ((1, 'B'), (2, 'B'), (3, 'B'), (4, 'D'), (5, 'C'), (6, 'A'))

    result = predictor.predict(clues_tuple)

    print("--- Full Predicted Answer Key ---")
    print(f"Key: {result['full_predicted_sequence']}\n")

    print("--- Detailed Probabilities for Unknown Questions ---")
    for q_num, data in result["predictions"].items():
        if data["status"] == "PREDICTED":
            probs_formatted = ", ".join([f"{k}: {v*100:.1f}%" for k, v in data["probabilities"].items()])
            print(f"Q{q_num:02d} -> Best: [{data['best_guess']}] | {probs_formatted}")