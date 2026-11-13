#!/usr/bin/env bash
# Static gate for the iOS packaging files. Runs on Linux; needs python3 with PyYAML and make.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

bash -n ios/build-deps.sh
bash -n ios/tools/check_ios.sh
python3 ios/tools/make_icon.py "$TMP/icon.png"
make -pn gles 2>/dev/null | sed -n 's/^SRC := //p' | tr ' ' '\n' | grep . | sort > "$TMP/makefile_src.txt"

python3 - "$TMP" <<'PY'
import glob, os, plistlib, struct, sys, yaml
tmp = sys.argv[1]
def need(c, m):
    if not c: sys.exit("FAIL: " + m)

# Info.plist
p = plistlib.load(open("ios/Info.plist", "rb"))
for k, v in {"CFBundleIconName": "AppIcon", "LSRequiresIPhoneOS": True, "UIFileSharingEnabled": True,
             "LSSupportsOpeningDocumentsInPlace": True, "UIRequiresFullScreen": True,
             "UIStatusBarHidden": True, "UIApplicationSupportsIndirectInputEvents": True,
             "UILaunchScreen": {}, "GCSupportsControllerUserInteraction": True,
             "GCSupportedGameControllers": [{"ProfileName": "ExtendedGamepad"}],
             "ITSAppUsesNonExemptEncryption": False,
             "LSApplicationCategoryType": "public.app-category.racing-games",
             "UIRequiredDeviceCapabilities": ["arm64", "opengles-2"]}.items():
    need(p.get(k) == v, "Info.plist " + k)
land = ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"]
need(p.get("UISupportedInterfaceOrientations") == land, "Info.plist orientations")
need(p.get("UISupportedInterfaceOrientations~ipad") == land, "Info.plist ipad orientations")
for k in ("CFBundleExecutable", "CFBundleIdentifier", "CFBundleShortVersionString", "CFBundleVersion", "CFBundleName"):
    need(k in p, "Info.plist " + k)
need("UIApplicationSceneManifest" not in p, "Info.plist must have no scene manifest")

# Privacy manifest
m = plistlib.load(open("ios/Resources/PrivacyInfo.xcprivacy", "rb"))
need(m["NSPrivacyTracking"] is False and m["NSPrivacyTrackingDomains"] == [] and m["NSPrivacyCollectedDataTypes"] == [], "privacy basics")
got = {a["NSPrivacyAccessedAPIType"]: a["NSPrivacyAccessedAPITypeReasons"] for a in m["NSPrivacyAccessedAPITypes"]}
need(got == {"NSPrivacyAccessedAPICategoryFileTimestamp": ["C617.1"],
             "NSPrivacyAccessedAPICategorySystemBootTime": ["35F9.1"],
             "NSPrivacyAccessedAPICategoryUserDefaults": ["CA92.1"]}, "privacy API reasons")

# Asset catalog
ic = __import__("json").load(open("ios/Resources/Assets.xcassets/AppIcon.appiconset/Contents.json"))
need(ic["images"] == [{"filename": "AppIcon-1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}], "appiconset Contents.json")

# Icon
d = open(tmp + "/icon.png", "rb").read()
need(d[:8] == b"\x89PNG\r\n\x1a\n", "icon signature")
w, h, bd, ct = struct.unpack(">IIBB", d[16:26])
need((w, h, bd, ct) == (1024, 1024, 8, 2), "icon must be 1024x1024 8-bit colour type 2, got %r" % ((w, h, bd, ct),))

# project.yml sources vs Makefile SRC
y = yaml.safe_load(open("ios/project.yml"))
t = y["targets"]["OpenUG2"]
src = t["sources"][0]
files = set()
for pat in src["includes"]:
    files |= {os.path.relpath(f, ".") for f in glob.glob(os.path.join("ios", src["path"], pat))}
for pat in src["excludes"]:
    files -= {os.path.relpath(f, ".") for f in glob.glob(os.path.join("ios", src["path"], pat))}
mk = set(open(tmp + "/makefile_src.txt").read().split())
need(files == mk, "project.yml sources differ from Makefile SRC: only-yml=%s only-make=%s" % (sorted(files - mk), sorted(mk - files)))
need(os.path.exists("ios/platform_ios.c"), "ios/platform_ios.c missing")
s = t["settings"]["base"]
need(s["PRODUCT_BUNDLE_IDENTIFIER"] == "com.tecaspt.openug2", "bundle id")
need(set(s["GCC_PREPROCESSOR_DEFINITIONS"]) == {"N2_GLES=1", "N2_IOS=1", "GLES_SILENCE_DEPRECATION=1"}, "defines")
need(t["scheme"] == {}, "scheme")

# codemagic.yaml
c = yaml.safe_load(open("codemagic.yaml"))
wf = c["workflows"]
need(set(wf) == {"ios-testflight", "ios-compile"}, "workflow names")
for n, w in wf.items():
    xc = str(w["environment"]["xcode"])
    need(xc.split(".")[0] == "26" and xc != "latest", n + " xcode must be pinned 26.x")
    need(w["instance_type"] == "mac_mini_m2", n + " instance")
need("triggering" not in wf["ios-compile"], "ios-compile must be manual")
tr = wf["ios-testflight"]["triggering"]
need(tr["events"] == ["push"] and any(b["pattern"] == "ios/main" and b["include"] for b in tr["branch_patterns"]), "testflight trigger")
need("ios_signing" not in {k for k in wf["ios-testflight"]["environment"] if k == "ios_signing"}, "no environment.ios_signing block")
pub = wf["ios-testflight"]["publishing"]["app_store_connect"]
need(pub["auth"] == "integration" and pub["submit_to_testflight"] is False and "beta_groups" not in pub, "publishing")
print("OK: python checks passed")
PY
echo "check_ios: PASS"
