Import("env")

def cpp_string(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return '\\"%s\\"' % escaped

ssid = env["ENV"].get("MESHCORE_WIFI_SSID", "")
password = env["ENV"].get("MESHCORE_WIFI_PASSWORD", "")
hostname = env["ENV"].get("MESHCORE_WIFI_HOSTNAME", "")

defines = []
if ssid:
    defines.append(("APP_WIFI_STA_SSID", cpp_string(ssid)))
if password:
    defines.append(("APP_WIFI_STA_PASSWORD", cpp_string(password)))
if hostname:
    defines.append(("APP_WIFI_OTA_HOSTNAME", cpp_string(hostname)))

if defines:
    env.Append(CPPDEFINES=defines)
