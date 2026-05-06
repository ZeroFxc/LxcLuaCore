function Decorator(target)
    if type(target) == "function" then
        return function(...) return target(...) end
    end
    target.decorated = true
    return target
end

@Decorator
class UserController {
}

print(UserController.decorated)
