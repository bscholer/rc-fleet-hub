import decimal
from datetime import datetime

def calculate_pi(digits):
    decimal.getcontext().prec = digits + 2
    
    def arctan_inv(x, prec):
        x = decimal.Decimal(x)
        power = x
        x_sq = x * x
        result = decimal.Decimal(0)
        for i in range(1, digits + 1):
            term = ((-1)**(i-1)) * power / (2*i - 1)
            if term == 0:
                break
            result += term
            power *= x_sq
        return result

    # Machin's formula: pi/4 = 4*arctan(1/5) - arctan(1/239)
    pi = 4 * (4 * arctan_inv(1/5, digits) - arctan_inv(1/239, digits))
    return str(pi).replace('0.', '')[:digits]

if __name__ == "__main__":
    start = datetime.now()
    calculate_pi(100000)
    print(datetime.now() - start)
