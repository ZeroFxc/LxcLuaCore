# 验证 modular inverse
two_y = 0x9075b4ee4d4788cabb49f7f81c221151fa2f68914d0aa833388fa11ff621a970
inv_two_y = 0x50ffd2a41556a5b8f09d65c8e98b0d3717638fa245c428774ecd2a4f8af1ba6a
p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F

# 计算 two_y * inv_two_y mod p
product = (two_y * inv_two_y) % p
print(f"two_y           = {two_y:064x}")
print(f"inv_two_y       = {inv_two_y:064x}")
print(f"product mod p   = {product:064x}")
print(f"expected        = {'0'*63}1")
print(f"correct?        = {product == 1}")

# 用 Python 的内置 pow 计算正确的逆
correct_inv = pow(two_y, -1, p)
print(f"\ncorrect inv     = {correct_inv:064x}")
print(f"our inv matches? = {correct_inv == inv_two_y}")

# 验证正确逆
verify = (two_y * correct_inv) % p
print(f"correct product = {verify:064x}")
print(f"correct?        = {verify == 1}")