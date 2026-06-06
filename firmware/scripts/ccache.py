Import("env")

original_path = env["ENV"].get("PATH", "")
env.PrependENVPath("PATH", "/usr/lib/ccache")
env["ENV"]["CCACHE_PATH"] = original_path
env["ENV"]["CCACHE_DIR"] = env["ENV"].get("CCACHE_DIR", "/home/developer/.ccache")
