from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = (root / "xmake.lua").read_text(encoding="utf-8")
body = source.split("after_build(function (target)", 1)[1].split("    end)", 1)[0]
script = """
function main()
    for _, ci in ipairs({true, false}) do
        local copies = 0
        local os = {
            getenv = function(key) if key == "GITHUB_ACTIONS" and ci then return "true" end end,
            isdir = function() return true end,
            trycp = function() copies = copies + 1; return true end
        }
        local path = {join = function(a, b) return a .. "/" .. b end}
        local print = function() end
        local deploy = function(target)
""" + body + """
        end
        deploy({targetfile = function() return "test.dll" end})
        assert(copies == (ci and 0 or 2), "unexpected deployment count: " .. copies)
    end
end
"""
with tempfile.TemporaryDirectory() as directory:
    probe = Path(directory) / "ci_deploy.lua"
    probe.write_text(script, encoding="utf-8")
    subprocess.run(["xmake", "lua", str(probe)], check=True, cwd=root)
print("CI deployment guard verified: zero copies in CI; local deployment preserved")
