local producer = || -> 42
local result2 = producer() |> |res| -> res + 10
print(result2)