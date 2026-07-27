class PilotLFSR:
    def __init__(self):
        self.INITIAL_STATE = 0x7F
        self.state = self.INITIAL_STATE
        self.POLARITY = [1, 1, 1, -1]

    def next_polarity(self):
        out_bit = (self.state >> 6) & 1
        feedback = out_bit ^ ((self.state >> 3) & 1)
        self.state = ((self.state << 1) | feedback) & 0x7F
        return feedback if out_bit == -1.0 else 1.0
    
    def reset(self):
        self.state = self.INITIAL_STATE