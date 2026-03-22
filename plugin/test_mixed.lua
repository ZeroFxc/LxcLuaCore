local plugin = require("plugin")
local os = require("os")

print("=== Testing Local Installation and Mixed Development ===")

-- 1. Install the pure Lua library
print("\n[Test] Installing pure .lua library...")
local res1 = plugin.install("plugin/sample_pkg_lib.lua")
print("Install result: " .. tostring(res1))

-- 2. Install the .plugin file
print("\n[Test] Installing .plugin file...")
local res2 = plugin.install("plugin/sample_pkg.plugin")
print("Install result: " .. tostring(res2))

-- 3. Require the mixed package
print("\n[Test] Requiring the installed mixed package...")
-- This will trigger the custom searcher, which loads the .plugin file,
-- which in turn will require("sample_pkg_lib") using the updated package.path.
local pkg = require("sample_pkg")

print("\n[Test] Verifying loaded package metadata...")
if pkg.name == "sample_pkg" and pkg.version == "1.0.0" and pkg.author == "lxclua developer" then
    print("Metadata parsed correctly!")
else
    print("Error: Metadata parsing failed or incomplete.")
end

print("\n[Test] Verifying mixed language library call result...")
if string.find(pkg.lib_result, "sample_pkg_lib") then
    print("Library function called successfully and returned expected result.")
else
    print("Error: Library function did not return expected result.")
end

print("\n[Test] Direct require of pure lua module from plugin directory...")
local direct_lib = require("sample_pkg_lib")
print("Directly called calculate(5, 7): " .. tostring(direct_lib.calculate(5, 7)))

print("\nAll tests completed successfully!")
