# 验证 3*G 的坐标是否在 secp256k1 曲线上
import sys

p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
a = 0
b = 7

Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8

n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

# 3*G from test output
pubkey_3g = "04f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9e51e970159c23cc65c3a7be6b99315110809cd9acd992f1edc9bce55af301705"
x_hex = pubkey_3g[2:66]
y_hex = pubkey_3g[66:130]
x3 = int(x_hex, 16)
y3 = int(y_hex, 16)
print(f"3*G x: {hex(x3)}")
print(f"3*G y: {hex(y3)}")

# 验证曲线: y^2 = x^3 + 7 (mod p)
lhs = (y3 * y3) % p
rhs = (x3 * x3 * x3 + 7) % p
print(f"y^2 mod p: {hex(lhs)}")
print(f"x^3+7 mod p: {hex(rhs)}")
print(f"On curve: {lhs == rhs}")

# 计算正确的 3*G
# 2*G
def point_double(x1, y1):
    lam = (3 * x1 * x1) * pow(2 * y1, -1, p) % p
    x3 = (lam * lam - 2 * x1) % p
    y3 = (lam * (x1 - x3) - y1) % p
    return (x3, y3)

def point_add(x1, y1, x2, y2):
    if x1 == x2 and y1 == y2:
        return point_double(x1, y1)
    lam = (y2 - y1) * pow(x2 - x1, -1, p) % p
    x3 = (lam * lam - x1 - x2) % p
    y3 = (lam * (x1 - x3) - y1) % p
    return (x3, y3)

g2x, g2y = point_double(Gx, Gy)
print(f"\n2*G correct: x={hex(g2x)}, y={hex(g2y)}")
print(f"2*G on curve: {(g2y*g2y) % p == (g2x*g2x*g2x + 7) % p}")

g3x, g3y = point_add(g2x, g2y, Gx, Gy)
print(f"\n3*G correct: x={hex(g3x)}, y={hex(g3y)}")
print(f"3*G on curve: {(g3y*g3y) % p == (g3x*g3x*g3x + 7) % p}")

# 检查从 C 代码输出的 3*G 是否匹配
print(f"\nC output 3*G x: {hex(x3)}")
print(f"Expected 3*G x: {hex(g3x)}")
print(f"x match: {x3 == g3x}")
print(f"C output 3*G y: {hex(y3)}")
print(f"Expected 3*G y: {hex(g3y)}")
print(f"y match: {y3 == g3y}")