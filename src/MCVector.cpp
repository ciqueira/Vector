// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#include "MCVector.h"

#include "MCLicense.h"
#include "MCNotice.h"
#include "VectorMath.h"
#include "VectorParams.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <cerrno>
#include <crt_externs.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"

#ifdef MC_NEXCONTROL_ENABLED
#include "MCNexControl.h"
namespace OFX {
namespace Private {
extern OfxHost *gHost;
}
} // namespace OFX
#endif

#ifndef PLUGIN_VERSION
#error "PLUGIN_VERSION must be defined from the Vector VERSION file"
#endif
#define kPluginVersion PLUGIN_VERSION

#define kPluginName "Vector"
#if defined(MC_NEXKEY_STAGING) && MC_NEXKEY_STAGING
#define kPluginNameLabel "Vector " kPluginVersion " (staging)"
#else
#define kPluginNameLabel "Vector " kPluginVersion
#endif
#define kPluginGrouping "MC Plugins"
#define kPluginDescription                                                     \
  "Parallel RGB curves saturation and split tone processor. The Input Space "  \
  "control sets the split-tone pivot preset and saturation model gamut."
#define kPluginIdentifier "com.MCVector"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

#define kSupportsTiles false
#define kSupportsMultiResolution false
#define kSupportsMultipleClipPARs false

#define kParamPivotPreset "pivotPreset"
#define kParamSaturationModelSpace "saturationModelSpace"
#define kParamEnableSaturation "enableSaturation"
#define kParamSatLow "satLow"
#define kParamSatMid "satMid"
#define kParamSatHigh "satHigh"
#define kParamSatGlobal "satGlobal"
#define kParamSatLumMask "satLumMask"
#define kParamShowSatCurve "showSatCurve"
#define kParamEnableZoneSaturation "enableZoneSaturation"
#define kParamZone "zone"
#define kParamZonePivot "zonePivot"
#define kParamZonePivotWidth "zonePivotWidth"
#define kParamZoneStrengthSat "zoneStrengthSat"
#define kParamShowZoneCurve "showZoneCurve"
#define kParamEnableSplitTone "enableSplitTone"
#define kParamSplitShadow "splitShadow"
#define kParamShadowMix "shadowMix"
#define kParamNeutralBlack "neutralBlack"
#define kParamSplitHighlight "splitHighlight"
#define kParamHighlightMix "highlightMix"
#define kParamNeutralWhite "neutralWhite"
#define kParamPivotWidth "pivotWidth"
#define kParamPivotOffset "pivotOffset"
#define kParamShadowCurveBias "shadowCurveBias"
#define kParamHighlightCurveBias "highlightCurveBias"
#define kParamShadowRedBellyCenter "shadowRedBellyCenter"
#define kParamShadowGreenBellyCenter "shadowGreenBellyCenter"
#define kParamShadowBlueBellyCenter "shadowBlueBellyCenter"
#define kParamShowToneCurve "showToneCurve"
#define kParamAboutHelp "aboutHelp"
#define kParamAppMCNexus "appMCNexus"
#define kParamLicenseRefreshBtn "licenseRefreshBtn"
#define kParamNoticeText "Update Plugin"
#define kParamNoticeUpdate "noticeUpdate"
#define kParamNoticeOpenNexus "noticeOpenNexus"

#ifdef MC_NEXKEY_DIAGNOSTICS
#define kParamDiagHost "diagHost"
#define kParamLicenseStatus "licenseStatus"
#define kParamLicenseEdition "licenseEdition"
#define kParamLicenseValidity "licenseValidity"
#define kParamLicenseActivation "licenseActivation"
#define kParamLicensePath "licensePath"
#define kParamLicenseSync "licenseSync"
#define kParamLicenseRefresh "licenseRefresh"
#endif

#define kAboutHelpUrl "https://github.com/ciqueira/Vector"

static constexpr double kShadowRedBellyCenterFixed = -0.705;
static constexpr double kShadowGreenBellyCenterFixed = -0.556;
static constexpr double kShadowBlueBellyCenterFixed = -0.626;

static bool startsWithAsciiCaseInsensitive(const char *str,
                                           const char *prefix) {
  while (*prefix != '\0') {
    if (*str == '\0') return false;
    const unsigned char c1 = static_cast<unsigned char>(*str);
    const unsigned char c2 = static_cast<unsigned char>(*prefix);
    if (std::tolower(c1) != std::tolower(c2)) return false;
    ++str;
    ++prefix;
  }
  return true;
}

static bool isSafeExternalUrl(const char *url) {
  if (!url || url[0] == '\0') return false;

  const char *afterScheme = nullptr;
  if (startsWithAsciiCaseInsensitive(url, "https://")) {
    afterScheme = url + 8;
  } else if (startsWithAsciiCaseInsensitive(url, "mcnexus://")) {
    afterScheme = url + 10;
  } else {
    return false;
  }

  if (*afterScheme == '\0') return false;

  std::size_t len = 0;
  for (const char *p = url; *p != '\0'; ++p) {
    if (++len > 2048) return false;
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c <= 0x20 || c >= 0x7F) return false;
    switch (*p) {
      case '"':
      case '\'':
      case '\\':
      case '`':
      case '$':
      case '<':
      case '>':
        return false;
      default:
        break;
    }
  }
  return true;
}

#ifdef MC_NEXKEY_ENABLED
static std::string urlEncodeQueryValue(const char *raw) {
  if (!raw) return {};
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(raw);
       *p != '\0'; ++p) {
    const unsigned char c = *p;
    const bool isUnreserved =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~';
    if (isUnreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[(c >> 4) & 0x0F]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}
#endif

#ifdef __APPLE__
static bool isMCNexusInstalledMac() {
  if (access("/Applications/MCNexus.app", F_OK) == 0) {
    return true;
  }
  const char *home = std::getenv("HOME");
  if (home && home[0] != '\0') {
    const std::string userApp =
        std::string(home) + "/Applications/MCNexus.app";
    if (access(userApp.c_str(), F_OK) == 0) {
      return true;
    }
  }
  return false;
}

static bool spawnOpenAndWait(const char *const argv[],
                             bool checkMCNexusOnEchild) {
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    return false;
  }

  int initStatus = posix_spawn_file_actions_addopen(
      &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  if (initStatus == 0) {
    initStatus = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  }
  if (initStatus == 0) {
    initStatus = posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  }

  pid_t pid = 0;
  const int rc =
      (initStatus == 0)
          ? posix_spawn(&pid, "/usr/bin/open", &actions, nullptr,
                        const_cast<char *const *>(argv), *_NSGetEnviron())
          : initStatus;

  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    return false;
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    if (errno == ECHILD) {
      return checkMCNexusOnEchild ? isMCNexusInstalledMac() : true;
    }
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#elif defined(_WIN32)
static bool tryOpenUrlSilentWindows(const std::string &utf8Url) {
  const int wideLen =
      MultiByteToWideChar(CP_UTF8, 0, utf8Url.c_str(), -1, nullptr, 0);
  if (wideLen <= 0) {
    return false;
  }
  std::wstring wideUrl(static_cast<std::size_t>(wideLen), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, utf8Url.c_str(), -1, &wideUrl[0],
                          wideLen) <= 0) {
    return false;
  }

  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_FLAG_NO_UI;
  sei.lpVerb = L"open";
  sei.lpFile = wideUrl.c_str();
  sei.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&sei) != FALSE;
}
#else
static bool spawnXdgOpenLinux(const char *url) {
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    return false;
  }
  int initStatus = posix_spawn_file_actions_addopen(
      &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  if (initStatus == 0) {
    initStatus = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  }
  if (initStatus == 0) {
    initStatus = posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  }

  pid_t pid = 0;
  char cmd[] = "xdg-open";
  std::string urlBuf(url);
  char *argv[] = {cmd, &urlBuf[0], nullptr};
  const int rc =
      (initStatus == 0)
          ? posix_spawnp(&pid, "xdg-open", &actions, nullptr, argv, environ)
          : initStatus;
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    return false;
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    if (errno == ECHILD) return true;
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

static int openUrlForNotice(const char *url) {
  if (!isSafeExternalUrl(url)) return 0;
#ifdef _WIN32
  return tryOpenUrlSilentWindows(url) ? 1 : 0;
#elif defined(__APPLE__)
  const bool isDeepLink = startsWithAsciiCaseInsensitive(url, "mcnexus://");
  const char *const args[] = {"open", "--", url, nullptr};
  return spawnOpenAndWait(args, isDeepLink) ? 1 : 0;
#else
  return spawnXdgOpenLinux(url) ? 1 : 0;
#endif
}

static void openExternalUrl(const char *url) {
  (void)openUrlForNotice(url);
}

static void openMCNexusApp() {
#ifdef MC_NEXKEY_ENABLED
  const std::string updatesDeepLink =
      std::string("mcnexus://updates/") + MC_NEXKEY_TENANT +
      "?variant=" + urlEncodeQueryValue(MC_NEXKEY_VARIANT);
  if (openUrlForNotice(updatesDeepLink.c_str())) {
    return;
  }
#endif

#ifdef __APPLE__
  const char *const appNameArgs[] = {"open", "-a", "MCNexus", nullptr};
  if (spawnOpenAndWait(appNameArgs, true)) {
    return;
  }
  const char *const appPathArgs[] = {"open", "/Applications/MCNexus.app",
                                     nullptr};
  if (spawnOpenAndWait(appPathArgs, true)) {
    return;
  }
  const char *const fallbackSiteArgs[] = {"open", "https://mcnexus.app",
                                          nullptr};
  spawnOpenAndWait(fallbackSiteArgs, false);
#elif defined(_WIN32)
  auto shellExecuteWindowsPath = [](const wchar_t *path,
                                    const wchar_t *parameters) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = path;
    sei.lpParameters = parameters;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
  };

  auto launchWindowsExecutableIfExists =
      [&](const wchar_t *pathWithEnvironment) {
        wchar_t expanded[MAX_PATH] = {};
        const DWORD expandedLength =
            ExpandEnvironmentStringsW(pathWithEnvironment, expanded, MAX_PATH);
        const wchar_t *path =
            (expandedLength > 0 && expandedLength < MAX_PATH)
                ? expanded
                : pathWithEnvironment;
        const DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
          return false;
        }
        return shellExecuteWindowsPath(path, nullptr);
      };

  if (launchWindowsExecutableIfExists(L"%ProgramFiles%\\MCNexus\\MCNexus.exe") ||
      launchWindowsExecutableIfExists(
          L"%ProgramFiles(x86)%\\MCNexus\\MCNexus.exe") ||
      launchWindowsExecutableIfExists(
          L"%LocalAppData%\\Programs\\MCNexus\\MCNexus.exe")) {
    return;
  }

  openExternalUrl(
      "https://apps.microsoft.com/detail/9n1qqt1xc825?hl=en-US&gl=US");
#else
  openExternalUrl("https://mcnexus.app");
#endif
}

#ifdef __APPLE__
extern "C" bool RunMetalKernel(
    void *commandQueue, int renderX1, int renderY1, int renderWidth,
    int renderHeight, int srcBoundsX1, int srcBoundsY1, int imgWidth,
    int imgHeight, int dstBoundsX1, int dstBoundsY1, int srcRowBytes,
    int dstRowBytes, int inputPremultiplied, int outputPremultiplied,
    const float *input, float *output, MCVectorParams params);
#else
extern "C" bool RunCudaKernel(
    void *stream, int renderX1, int renderY1, int renderWidth, int renderHeight,
    int srcBoundsX1, int srcBoundsY1, int imgWidth, int imgHeight,
    int dstBoundsX1, int dstBoundsY1, int srcRowBytes, int dstRowBytes,
    int inputPremultiplied, int outputPremultiplied, const float *input,
    float *output, MCVectorParams params);
#endif

class MCVectorProcessor : public OFX::ImageProcessor {
public:
  explicit MCVectorProcessor(OFX::ImageEffect &instance)
      : OFX::ImageProcessor(instance), _srcImg(nullptr) {
    std::memset(&_params, 0, sizeof(_params));
  }

  void setSrcImg(OFX::Image *img) { _srcImg = img; }
  void setParams(const MCVectorParams &params) { _params = params; }

  virtual void processImagesCuda() override {
#ifndef __APPLE__
    if (!_srcImg || !_dstImg) {
      OFX::throwSuiteStatusException(kOfxStatErrValue);
    }
    const OfxRectI &srcBounds = _srcImg->getBounds();
    const OfxRectI &dstBounds = _dstImg->getBounds();
    const int renderX1 =
        std::max(_renderWindow.x1, std::max(srcBounds.x1, dstBounds.x1));
    const int renderY1 =
        std::max(_renderWindow.y1, std::max(srcBounds.y1, dstBounds.y1));
    const int renderX2 =
        std::min(_renderWindow.x2, std::min(srcBounds.x2, dstBounds.x2));
    const int renderY2 =
        std::min(_renderWindow.y2, std::min(srcBounds.y2, dstBounds.y2));
    if (renderX1 >= renderX2 || renderY1 >= renderY2) {
      return;
    }

    const int srcRowBytes = _srcImg->getRowBytes();
    const int dstRowBytes = _dstImg->getRowBytes();
    if (srcRowBytes <= 0 || dstRowBytes <= 0 ||
        (srcRowBytes % static_cast<int>(sizeof(float))) != 0 ||
        (dstRowBytes % static_cast<int>(sizeof(float))) != 0) {
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    const int imgWidth = srcBounds.x2 - srcBounds.x1;
    const int imgHeight = srcBounds.y2 - srcBounds.y1;
    const float *input = static_cast<const float *>(_srcImg->getPixelData());
    float *output = static_cast<float *>(_dstImg->getPixelData());
    const bool inputPremultiplied =
        _srcImg->getPreMultiplication() == OFX::eImagePreMultiplied;
    const bool outputPremultiplied =
        _dstImg->getPreMultiplication() == OFX::eImagePreMultiplied;

    const bool succeeded = RunCudaKernel(
        _pCudaStream, renderX1, renderY1, renderX2 - renderX1,
        renderY2 - renderY1, srcBounds.x1, srcBounds.y1, imgWidth, imgHeight,
        dstBounds.x1, dstBounds.y1, srcRowBytes, dstRowBytes,
        inputPremultiplied ? 1 : 0, outputPremultiplied ? 1 : 0, input, output,
        _params);
    if (!succeeded) {
      OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#else
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
#endif
  }

  virtual void processImagesMetal() override {
#ifdef __APPLE__
    if (!_srcImg || !_dstImg) {
      OFX::throwSuiteStatusException(kOfxStatErrValue);
    }
    const OfxRectI &srcBounds = _srcImg->getBounds();
    const OfxRectI &dstBounds = _dstImg->getBounds();
    const int renderX1 =
        std::max(_renderWindow.x1, std::max(srcBounds.x1, dstBounds.x1));
    const int renderY1 =
        std::max(_renderWindow.y1, std::max(srcBounds.y1, dstBounds.y1));
    const int renderX2 =
        std::min(_renderWindow.x2, std::min(srcBounds.x2, dstBounds.x2));
    const int renderY2 =
        std::min(_renderWindow.y2, std::min(srcBounds.y2, dstBounds.y2));
    if (renderX1 >= renderX2 || renderY1 >= renderY2) {
      return;
    }

    const int srcRowBytes = _srcImg->getRowBytes();
    const int dstRowBytes = _dstImg->getRowBytes();
    if (srcRowBytes <= 0 || dstRowBytes <= 0 ||
        (srcRowBytes % static_cast<int>(sizeof(float))) != 0 ||
        (dstRowBytes % static_cast<int>(sizeof(float))) != 0) {
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    const int imgWidth = srcBounds.x2 - srcBounds.x1;
    const int imgHeight = srcBounds.y2 - srcBounds.y1;
    const float *input = static_cast<const float *>(_srcImg->getPixelData());
    float *output = static_cast<float *>(_dstImg->getPixelData());
    const bool inputPremultiplied =
        _srcImg->getPreMultiplication() == OFX::eImagePreMultiplied;
    const bool outputPremultiplied =
        _dstImg->getPreMultiplication() == OFX::eImagePreMultiplied;

    const bool succeeded = RunMetalKernel(
        _pMetalCmdQ, renderX1, renderY1, renderX2 - renderX1,
        renderY2 - renderY1, srcBounds.x1, srcBounds.y1, imgWidth, imgHeight,
        dstBounds.x1, dstBounds.y1, srcRowBytes, dstRowBytes,
        inputPremultiplied ? 1 : 0, outputPremultiplied ? 1 : 0, input, output,
        _params);
    if (!succeeded) {
      OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#else
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
#endif
  }

  virtual void multiThreadProcessImages(OfxRectI procWindow) override {
    using namespace mcvector;

    if (!_srcImg || !_dstImg) {
      return;
    }

    const OfxRectI &srcBounds = _srcImg->getBounds();
    const OfxRectI &dstBounds = _dstImg->getBounds();
    const int x1 = std::max(procWindow.x1, std::max(srcBounds.x1, dstBounds.x1));
    const int y1 = std::max(procWindow.y1, std::max(srcBounds.y1, dstBounds.y1));
    const int x2 = std::min(procWindow.x2, std::min(srcBounds.x2, dstBounds.x2));
    const int y2 = std::min(procWindow.y2, std::min(srcBounds.y2, dstBounds.y2));
    if (x1 >= x2 || y1 >= y2) {
      return;
    }

    const int width = srcBounds.x2 - srcBounds.x1;
    const int height = srcBounds.y2 - srcBounds.y1;
    const bool inputPremultiplied =
        _srcImg->getPreMultiplication() == OFX::eImagePreMultiplied;
    const bool outputPremultiplied =
        _dstImg->getPreMultiplication() == OFX::eImagePreMultiplied;
    const bool fastPassthrough =
        vectorIsIdentity(_params) && inputPremultiplied == outputPremultiplied;
    const int rowWidth = x2 - x1;

    for (int y = y1; y < y2; ++y) {
      if (_effect.abort()) {
        break;
      }

      const float *srcRow =
          static_cast<const float *>(_srcImg->getPixelAddress(x1, y));
      float *dstRow = static_cast<float *>(_dstImg->getPixelAddress(x1, y));
      if (!srcRow || !dstRow) {
        continue;
      }

      if (fastPassthrough) {
        std::memcpy(dstRow, srcRow,
                    static_cast<std::size_t>(rowWidth) * 4 * sizeof(float));
        continue;
      }

      for (int x = 0; x < rowWidth; ++x) {
        const int imageX = x1 + x - srcBounds.x1;
        const int imageY = y - srcBounds.y1;
        const float alpha = srcRow[x * 4 + 3];
        float3 in =
            make_float3(srcRow[x * 4 + 0], srcRow[x * 4 + 1],
                        srcRow[x * 4 + 2]);
        if (inputPremultiplied) {
          if (alpha > kEpsilon) {
            in.x /= alpha;
            in.y /= alpha;
            in.z /= alpha;
          } else {
            in = make_float3(0.0f, 0.0f, 0.0f);
          }
        }

        float3 out = applyVector(in, _params);
        float overlayAlpha = 0.0f;
        out = drawToneCurve(out, imageX, imageY, width, height, _params,
                            overlayAlpha);
        out = drawSatCurve(out, imageX, imageY, width, height, _params,
                           overlayAlpha);
        out = drawZoneCurve(out, imageX, imageY, width, height, _params,
                            overlayAlpha);

        const float outputAlpha = std::max(alpha, overlayAlpha);
        if (outputPremultiplied) {
          out.x *= outputAlpha;
          out.y *= outputAlpha;
          out.z *= outputAlpha;
        }

        dstRow[x * 4 + 0] = out.x;
        dstRow[x * 4 + 1] = out.y;
        dstRow[x * 4 + 2] = out.z;
        dstRow[x * 4 + 3] = outputAlpha;
      }
    }
  }

private:
  OFX::Image *_srcImg;
  MCVectorParams _params;
};

class MCVectorPlugin : public OFX::ImageEffect {
public:
  explicit MCVectorPlugin(OfxImageEffectHandle handle);
  virtual ~MCVectorPlugin();

  virtual void render(const OFX::RenderArguments &args) override;
  virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                          OFX::Clip *&identityClip,
                          double &identityTime) override;
  virtual void changedParam(const OFX::InstanceChangedArgs &args,
                            const std::string &paramName) override;
  virtual void beginEdit(void) override;

private:
  MCVectorParams getActiveParams(double time);
  static MCVectorParams neutralParams();
  void setupAndProcess(MCVectorProcessor &processor,
                       const OFX::RenderArguments &args);
  void applyLicenseGate();
  void syncLicenseGateIfChanged();
  void publishNotice();
#ifdef MC_NEXKEY_DIAGNOSTICS
  void publishLicenseReport();
#endif

  OFX::Clip *m_SrcClip = nullptr;
  OFX::Clip *m_DstClip = nullptr;

  OFX::ChoiceParam *m_PivotPreset = nullptr;
  OFX::ChoiceParam *m_SaturationModelSpace = nullptr;
  OFX::GroupParam *m_SplitToneGroup = nullptr;
  OFX::BooleanParam *m_EnableSaturation = nullptr;
  OFX::DoubleParam *m_SatLow = nullptr;
  OFX::DoubleParam *m_SatMid = nullptr;
  OFX::DoubleParam *m_SatHigh = nullptr;
  OFX::DoubleParam *m_SatGlobal = nullptr;
  OFX::DoubleParam *m_SatLumMask = nullptr;
  OFX::BooleanParam *m_ShowSatCurve = nullptr;
  OFX::GroupParam *m_SaturationGroup = nullptr;
  OFX::BooleanParam *m_EnableZoneSaturation = nullptr;
  OFX::DoubleParam *m_Zone = nullptr;
  OFX::DoubleParam *m_ZonePivot = nullptr;
  OFX::DoubleParam *m_ZonePivotWidth = nullptr;
  OFX::DoubleParam *m_ZoneStrengthSat = nullptr;
  OFX::BooleanParam *m_ShowZoneCurve = nullptr;
  OFX::GroupParam *m_ZoneSaturationGroup = nullptr;
  OFX::BooleanParam *m_EnableSplitTone = nullptr;
  OFX::DoubleParam *m_SplitShadow = nullptr;
  OFX::DoubleParam *m_ShadowMix = nullptr;
  OFX::DoubleParam *m_NeutralBlack = nullptr;
  OFX::DoubleParam *m_SplitHighlight = nullptr;
  OFX::DoubleParam *m_HighlightMix = nullptr;
  OFX::DoubleParam *m_NeutralWhite = nullptr;
  OFX::DoubleParam *m_PivotWidth = nullptr;
  OFX::DoubleParam *m_PivotOffset = nullptr;
  OFX::DoubleParam *m_ShadowCurveBias = nullptr;
  OFX::DoubleParam *m_HighlightCurveBias = nullptr;
  OFX::DoubleParam *m_ShadowRedBellyCenter = nullptr;
  OFX::DoubleParam *m_ShadowGreenBellyCenter = nullptr;
  OFX::DoubleParam *m_ShadowBlueBellyCenter = nullptr;
  OFX::BooleanParam *m_ShowToneCurve = nullptr;

  OFX::GroupParam *m_NoticeGroup = nullptr;
  OFX::StringParam *m_NoticeText = nullptr;
  OFX::PushButtonParam *m_NoticeUpdate = nullptr;
  OFX::PushButtonParam *m_NoticeOpenNexus = nullptr;

#ifdef MC_NEXKEY_DIAGNOSTICS
  OFX::StringParam *m_DiagHost = nullptr;
  OFX::StringParam *m_LicenseStatus = nullptr;
  OFX::StringParam *m_LicenseEdition = nullptr;
  OFX::StringParam *m_LicenseValidity = nullptr;
  OFX::StringParam *m_LicenseActivation = nullptr;
  OFX::StringParam *m_LicenseSync = nullptr;
  OFX::StringParam *m_LicensePath = nullptr;
#endif

  bool m_GateHidden = false;
  bool m_Publishing = false;

  mc::License m_License;
  mc::Notices m_Notices;

#ifdef MC_NEXCONTROL_ENABLED
  OFX::StringParam *m_InstanceInfo = nullptr;
  std::unique_ptr<mc::nexcontrol::InstanceBinding> m_nexcontrolBinding;
#endif
};

MCVectorPlugin::MCVectorPlugin(OfxImageEffectHandle handle)
    : OFX::ImageEffect(handle) {
  m_DstClip = fetchClip(kOfxImageEffectOutputClipName);
  m_SrcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

  m_NoticeGroup = fetchGroupParam("grpNotice");
  m_NoticeText = fetchStringParam(kParamNoticeText);
  m_NoticeUpdate = fetchPushButtonParam(kParamNoticeUpdate);
  m_NoticeOpenNexus = fetchPushButtonParam(kParamNoticeOpenNexus);

#ifdef MC_NEXKEY_DIAGNOSTICS
  m_DiagHost = fetchStringParam(kParamDiagHost);
  m_LicenseStatus = fetchStringParam(kParamLicenseStatus);
  m_LicenseEdition = fetchStringParam(kParamLicenseEdition);
  m_LicenseValidity = fetchStringParam(kParamLicenseValidity);
  m_LicenseActivation = fetchStringParam(kParamLicenseActivation);
  m_LicenseSync = fetchStringParam(kParamLicenseSync);
  m_LicensePath = fetchStringParam(kParamLicensePath);
#endif

  m_License.start();

  {
    const OFX::ImageEffectHostDescription *host =
        OFX::getImageEffectHostDescription();
    std::string hostVersion;
    if (host) {
      char buffer[48];
      std::snprintf(buffer, sizeof(buffer), "%d.%d.%d", host->versionMajor,
                    host->versionMinor, host->versionMicro);
      hostVersion = buffer;
    }
    m_Notices.start(&openUrlForNotice, host ? host->hostName.c_str() : nullptr,
                    hostVersion.c_str());
  }

  m_PivotPreset = fetchChoiceParam(kParamPivotPreset);
  m_SaturationModelSpace = fetchChoiceParam(kParamSaturationModelSpace);

  m_SplitToneGroup = fetchGroupParam("grpSplitTone");
  m_EnableSplitTone = fetchBooleanParam(kParamEnableSplitTone);
  m_SplitShadow = fetchDoubleParam(kParamSplitShadow);
  m_ShadowMix = fetchDoubleParam(kParamShadowMix);
  m_NeutralBlack = fetchDoubleParam(kParamNeutralBlack);
  m_SplitHighlight = fetchDoubleParam(kParamSplitHighlight);
  m_HighlightMix = fetchDoubleParam(kParamHighlightMix);
  m_NeutralWhite = fetchDoubleParam(kParamNeutralWhite);
  m_PivotWidth = fetchDoubleParam(kParamPivotWidth);
  m_PivotOffset = fetchDoubleParam(kParamPivotOffset);
  m_ShadowCurveBias = fetchDoubleParam(kParamShadowCurveBias);
  m_HighlightCurveBias = fetchDoubleParam(kParamHighlightCurveBias);
  m_ShadowRedBellyCenter = fetchDoubleParam(kParamShadowRedBellyCenter);
  m_ShadowGreenBellyCenter = fetchDoubleParam(kParamShadowGreenBellyCenter);
  m_ShadowBlueBellyCenter = fetchDoubleParam(kParamShadowBlueBellyCenter);
  m_ShowToneCurve = fetchBooleanParam(kParamShowToneCurve);

  m_SaturationGroup = fetchGroupParam("grpSaturation");
  m_EnableSaturation = fetchBooleanParam(kParamEnableSaturation);
  m_SatLow = fetchDoubleParam(kParamSatLow);
  m_SatMid = fetchDoubleParam(kParamSatMid);
  m_SatHigh = fetchDoubleParam(kParamSatHigh);
  m_SatGlobal = fetchDoubleParam(kParamSatGlobal);
  m_SatLumMask = fetchDoubleParam(kParamSatLumMask);
  m_ShowSatCurve = fetchBooleanParam(kParamShowSatCurve);

  m_ZoneSaturationGroup = fetchGroupParam("grpZoneSaturation");
  m_EnableZoneSaturation = fetchBooleanParam(kParamEnableZoneSaturation);
  m_Zone = fetchDoubleParam(kParamZone);
  m_ZonePivot = fetchDoubleParam(kParamZonePivot);
  m_ZonePivotWidth = fetchDoubleParam(kParamZonePivotWidth);
  m_ZoneStrengthSat = fetchDoubleParam(kParamZoneStrengthSat);
  m_ShowZoneCurve = fetchBooleanParam(kParamShowZoneCurve);

  m_Notices.waitForCheck(mc::kCheckWaitBudgetMs);

  m_Publishing = true;
  applyLicenseGate();
  publishNotice();
#ifdef MC_NEXKEY_DIAGNOSTICS
  publishLicenseReport();
#endif
  m_Publishing = false;

#ifdef MC_NEXCONTROL_ENABLED
  if (OFX::Private::gHost) {
    mc::nexcontrol::init(OFX::Private::gHost, m_License.handle());
  }

  m_nexcontrolBinding = std::make_unique<mc::nexcontrol::InstanceBinding>(handle);
  {
    auto batch = m_nexcontrolBinding->beginBatch();

    auto splitGroup = m_nexcontrolBinding->beginGroup(
        "Split Tone", mc::nexcontrol::GroupLayout::DedicatedBank);
    splitGroup.addParam(kParamSplitShadow, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0,
                        0.0, 1.0, 0.0);
    splitGroup.addParam(kParamShadowMix, NEXCONTROL_PARAM_DOUBLE, -1.0, 1.0,
                        -1.0, 1.0, 0.0, false, NEXCONTROL_PARAM_FLAG_BIPOLAR);
    splitGroup.addParam(kParamNeutralBlack, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0,
                        0.0, 1.0, 1.0);
    splitGroup.addParam(kParamSplitHighlight, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0,
                        0.0, 1.0, 0.0);
    splitGroup.addParam(kParamHighlightMix, NEXCONTROL_PARAM_DOUBLE, -1.0, 1.0,
                        -1.0, 1.0, 0.0, false, NEXCONTROL_PARAM_FLAG_BIPOLAR);
    splitGroup.addParam(kParamNeutralWhite, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0,
                        0.0, 1.0, 1.0);
    splitGroup.addParam(kParamPivotWidth, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0,
                        0.0, 1.0, 0.0);
    splitGroup.addParam(kParamPivotOffset, NEXCONTROL_PARAM_DOUBLE, -1.0, 1.0,
                        -1.0, 1.0, 0.0, false, NEXCONTROL_PARAM_FLAG_BIPOLAR);
    splitGroup.addParam(kParamShadowCurveBias, NEXCONTROL_PARAM_DOUBLE, -1.0,
                        1.0, -1.0, 1.0, 0.0, false,
                        NEXCONTROL_PARAM_FLAG_BIPOLAR);
    splitGroup.addParam(kParamHighlightCurveBias, NEXCONTROL_PARAM_DOUBLE, -1.0,
                        1.0, -1.0, 1.0, 0.0, false,
                        NEXCONTROL_PARAM_FLAG_BIPOLAR);

    auto satGroup = m_nexcontrolBinding->beginGroup(
        "Curves Saturation", mc::nexcontrol::GroupLayout::DedicatedBank);
    satGroup.addParam(kParamSatGlobal, NEXCONTROL_PARAM_DOUBLE, 0.0, 2.0, 0.0,
                      2.0, 1.0);
    satGroup.addParam(kParamSatLow, NEXCONTROL_PARAM_DOUBLE, 0.0, 2.0, 0.0,
                      2.0, 1.0);
    satGroup.addParam(kParamSatMid, NEXCONTROL_PARAM_DOUBLE, 0.0, 2.0, 0.0,
                      2.0, 1.0);
    satGroup.addParam(kParamSatHigh, NEXCONTROL_PARAM_DOUBLE, 0.0, 2.0, 0.0,
                      2.0, 1.0);
    satGroup.addParam(kParamSatLumMask, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0, 0.0,
                      1.0, 1.0);

    auto zoneGroup = m_nexcontrolBinding->beginGroup(
        "Zone Saturation", mc::nexcontrol::GroupLayout::DedicatedBank);
    zoneGroup.addParam(kParamZone, NEXCONTROL_PARAM_DOUBLE, -1.0, 1.0, -1.0,
                       1.0, 0.0, false, NEXCONTROL_PARAM_FLAG_BIPOLAR);
    zoneGroup.addParam(kParamZonePivot, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0, 0.0,
                       1.0, 0.5);
    zoneGroup.addParam(kParamZonePivotWidth, NEXCONTROL_PARAM_DOUBLE, 0.0, 1.0,
                       0.0, 1.0, 0.5);
    zoneGroup.addParam(kParamZoneStrengthSat, NEXCONTROL_PARAM_DOUBLE, -1.0,
                       1.0, -1.0, 1.0, 0.0, false,
                       NEXCONTROL_PARAM_FLAG_BIPOLAR);
  }

  m_InstanceInfo = fetchStringParam("instance_info");
  if (m_InstanceInfo && m_nexcontrolBinding) {
    const std::string info =
        "Node Instance (ID: " +
        std::to_string(m_nexcontrolBinding->instance_id()) + ")";
    m_Publishing = true;
    try {
      m_InstanceInfo->setValue(info);
    } catch (...) {
    }
    m_Publishing = false;
  }
#endif
}

MCVectorPlugin::~MCVectorPlugin() {
#ifdef MC_NEXCONTROL_ENABLED
  m_nexcontrolBinding.reset();
#endif
}

MCVectorParams MCVectorPlugin::neutralParams() {
  MCVectorParams p;
  std::memset(&p, 0, sizeof(p));
  p.pivotPreset = 1;
  p.saturationModelSpace = 1;
  p.satLow = 1.0f;
  p.satMid = 1.0f;
  p.satHigh = 1.0f;
  p.satGlobal = 1.0f;
  p.satLumMask = 1.0f;
  p.zonePivot = 0.5f;
  p.zoneSoftness = 0.5f;
  p.neutralBlack = 1.0f;
  p.neutralWhite = 1.0f;
  p.shadowRedBellyCenter = static_cast<float>(kShadowRedBellyCenterFixed);
  p.shadowGreenBellyCenter = static_cast<float>(kShadowGreenBellyCenterFixed);
  p.shadowBlueBellyCenter = static_cast<float>(kShadowBlueBellyCenterFixed);
  return p;
}

MCVectorParams MCVectorPlugin::getActiveParams(double time) {
  MCVectorParams p;
  std::memset(&p, 0, sizeof(p));

  int pivotPreset = 1;
  m_PivotPreset->getValueAtTime(time, pivotPreset);
  p.pivotPreset = pivotPreset;
  int saturationModelSpace = 1;
  m_SaturationModelSpace->getValueAtTime(time, saturationModelSpace);
  p.saturationModelSpace = saturationModelSpace;
  p.enableSaturation = m_EnableSaturation->getValueAtTime(time) ? 1 : 0;
  p.enableZoneSaturation =
      m_EnableZoneSaturation->getValueAtTime(time) ? 1 : 0;
  p.enableSplitTone = m_EnableSplitTone->getValueAtTime(time) ? 1 : 0;
  p.showSatCurve = m_ShowSatCurve->getValueAtTime(time) ? 1 : 0;
  p.showToneCurve = m_ShowToneCurve->getValueAtTime(time) ? 1 : 0;
  p.showZoneCurve = m_ShowZoneCurve->getValueAtTime(time) ? 1 : 0;

  p.satLow = static_cast<float>(m_SatLow->getValueAtTime(time));
  p.satMid = static_cast<float>(m_SatMid->getValueAtTime(time));
  p.satHigh = static_cast<float>(m_SatHigh->getValueAtTime(time));
  p.satGlobal = static_cast<float>(m_SatGlobal->getValueAtTime(time));
  p.satLumMask = static_cast<float>(m_SatLumMask->getValueAtTime(time));
  p.zoneShadowSaturation = static_cast<float>(m_Zone->getValueAtTime(time));
  p.zoneHighlightSaturation =
      static_cast<float>(m_ZoneStrengthSat->getValueAtTime(time));
  p.zonePivot = static_cast<float>(m_ZonePivot->getValueAtTime(time));
  p.zoneSoftness = static_cast<float>(m_ZonePivotWidth->getValueAtTime(time));

  p.splitShadow = static_cast<float>(m_SplitShadow->getValueAtTime(time));
  p.shadowMix = static_cast<float>(m_ShadowMix->getValueAtTime(time));
  p.neutralBlack =
      static_cast<float>(0.5 + m_NeutralBlack->getValueAtTime(time) * 0.5);
  p.splitHighlight =
      static_cast<float>(m_SplitHighlight->getValueAtTime(time));
  p.highlightMix = static_cast<float>(m_HighlightMix->getValueAtTime(time));
  p.neutralWhite = static_cast<float>(m_NeutralWhite->getValueAtTime(time));
  p.pivotWidth = static_cast<float>(m_PivotWidth->getValueAtTime(time));
  p.pivotOffset = static_cast<float>(m_PivotOffset->getValueAtTime(time));
  p.shadowCurveBias =
      static_cast<float>(m_ShadowCurveBias->getValueAtTime(time) * 0.6);
  p.highlightCurveBias =
      static_cast<float>(m_HighlightCurveBias->getValueAtTime(time) * 0.2);
  p.shadowRedBellyCenter = static_cast<float>(kShadowRedBellyCenterFixed);
  p.shadowGreenBellyCenter = static_cast<float>(kShadowGreenBellyCenterFixed);
  p.shadowBlueBellyCenter = static_cast<float>(kShadowBlueBellyCenterFixed);
  return p;
}

void MCVectorPlugin::applyLicenseGate() {
  const bool hidden = !m_License.allowed();
  m_GateHidden = hidden;

  if (m_PivotPreset) m_PivotPreset->setIsSecret(hidden);
  if (m_SaturationModelSpace) m_SaturationModelSpace->setIsSecret(hidden);

  if (m_SplitToneGroup) m_SplitToneGroup->setIsSecret(hidden);
  if (m_EnableSplitTone) m_EnableSplitTone->setIsSecret(hidden);
  if (m_SplitShadow) m_SplitShadow->setIsSecret(hidden);
  if (m_ShadowMix) m_ShadowMix->setIsSecret(hidden);
  if (m_NeutralBlack) m_NeutralBlack->setIsSecret(hidden);
  if (m_SplitHighlight) m_SplitHighlight->setIsSecret(hidden);
  if (m_HighlightMix) m_HighlightMix->setIsSecret(hidden);
  if (m_NeutralWhite) m_NeutralWhite->setIsSecret(hidden);
  if (m_PivotWidth) m_PivotWidth->setIsSecret(hidden);
  if (m_PivotOffset) m_PivotOffset->setIsSecret(hidden);
  if (m_ShadowCurveBias) m_ShadowCurveBias->setIsSecret(hidden);
  if (m_HighlightCurveBias) m_HighlightCurveBias->setIsSecret(hidden);
  if (m_ShowToneCurve) m_ShowToneCurve->setIsSecret(hidden);
  // Keep m_ShadowRedBellyCenter, m_ShadowGreenBellyCenter and
  // m_ShadowBlueBellyCenter permanently secret.

  if (m_SaturationGroup) m_SaturationGroup->setIsSecret(hidden);
  if (m_EnableSaturation) m_EnableSaturation->setIsSecret(hidden);
  if (m_SatLow) m_SatLow->setIsSecret(hidden);
  if (m_SatMid) m_SatMid->setIsSecret(hidden);
  if (m_SatHigh) m_SatHigh->setIsSecret(hidden);
  if (m_SatGlobal) m_SatGlobal->setIsSecret(hidden);
  if (m_SatLumMask) m_SatLumMask->setIsSecret(hidden);
  if (m_ShowSatCurve) m_ShowSatCurve->setIsSecret(hidden);

  if (m_ZoneSaturationGroup) m_ZoneSaturationGroup->setIsSecret(hidden);
  if (m_EnableZoneSaturation) m_EnableZoneSaturation->setIsSecret(hidden);
  if (m_Zone) m_Zone->setIsSecret(hidden);
  if (m_ZonePivot) m_ZonePivot->setIsSecret(hidden);
  if (m_ZonePivotWidth) m_ZonePivotWidth->setIsSecret(hidden);
  if (m_ZoneStrengthSat) m_ZoneStrengthSat->setIsSecret(hidden);
  if (m_ShowZoneCurve) m_ShowZoneCurve->setIsSecret(hidden);
}

void MCVectorPlugin::syncLicenseGateIfChanged() {
  if (m_GateHidden == !m_License.allowed()) return;

  m_Publishing = true;
  applyLicenseGate();
  publishNotice();
#ifdef MC_NEXKEY_DIAGNOSTICS
  publishLicenseReport();
#endif
  m_Publishing = false;
}

void MCVectorPlugin::publishNotice() {
  const mc::NoticeView view =
      m_License.allowed() ? m_Notices.view() : mc::NoticeView();

  if (m_NoticeText) m_NoticeText->setValue(view.text);
  if (m_NoticeGroup) m_NoticeGroup->setIsSecret(!view.visible);
  if (m_NoticeUpdate) m_NoticeUpdate->setIsSecret(!view.offerUpdate);
  if (m_NoticeOpenNexus) m_NoticeOpenNexus->setIsSecret(!view.offerOpenNexus);
}

#ifdef MC_NEXKEY_DIAGNOSTICS
void MCVectorPlugin::publishLicenseReport() {
  const mc::LicenseReport report = m_License.report();

  if (m_DiagHost) {
    const OFX::ImageEffectHostDescription *host =
        OFX::getImageEffectHostDescription();
    if (!host) {
      m_DiagHost->setValue("(host did not describe itself)");
    } else {
      char buffer[192];
      std::snprintf(buffer, sizeof(buffer), "%s  %d.%d.%d  (\"%s\")",
                    host->hostName.c_str(), host->versionMajor,
                    host->versionMinor, host->versionMicro,
                    host->versionLabel.c_str());
      m_DiagHost->setValue(buffer);
    }
  }

  if (m_LicenseStatus) {
    std::string text = report.status;
    text += report.allowed ? "  (allow)" : "  (deny)";
    if (!report.enforcing) {
      text += report.allowed ? "  — shadow mode"
                             : "  — shadow mode, effect STILL APPLIED";
    }
    m_LicenseStatus->setValue(text);
  }
  if (m_LicenseEdition) {
    m_LicenseEdition->setValue(report.edition.empty() ? "-" : report.edition);
  }
  if (m_LicenseValidity) {
    m_LicenseValidity->setValue(report.validity.empty() ? "-" : report.validity);
  }
  if (m_LicenseActivation) {
    m_LicenseActivation->setValue(
        report.activation.empty() ? "-" : report.activation);
  }
  if (m_LicenseSync) {
    m_LicenseSync->setValue(report.sync.empty() ? "-" : report.sync);
  }

  if (m_LicensePath) {
    std::string text = report.receiptsDir;
    if (text.empty()) {
      text = "(unresolved)";
    } else if (report.receiptsFound < 0) {
      text += "  [unreadable]";
    } else {
      char suffix[32];
      std::snprintf(suffix, sizeof(suffix), "  [%d receipt(s)]",
                    report.receiptsFound);
      text += suffix;
    }
    m_LicensePath->setValue(text);
  }
}
#endif

void MCVectorPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                  const std::string &paramName) {
  if (m_Publishing) return;

  if (paramName == kParamAboutHelp) {
    openExternalUrl(kAboutHelpUrl);
  } else if (paramName == kParamAppMCNexus) {
    openMCNexusApp();
  } else if (paramName == kParamNoticeUpdate ||
             paramName == kParamNoticeOpenNexus) {
    m_Notices.openAction();
  } else if (paramName == kParamLicenseRefreshBtn) {
    m_License.requestUserRefresh();
    m_Notices.requestUserCheck();
  }
#ifdef MC_NEXKEY_DIAGNOSTICS
  else if (paramName == kParamLicenseSync "Btn") {
    m_License.syncNow();
  } else if (paramName == kParamLicenseRefresh) {
    m_License.refresh();
  }
#endif

  if (args.reason != OFX::eChangeUserEdit) {
    syncLicenseGateIfChanged();
    return;
  }

  m_Publishing = true;
  const bool gateWas = m_GateHidden;
  m_License.refreshIfStale();
  applyLicenseGate();

  const bool noticeSettled = m_Notices.takeSettled();
  if (noticeSettled || m_GateHidden != gateWas) {
    publishNotice();
  }
#ifdef MC_NEXKEY_DIAGNOSTICS
  publishLicenseReport();
#endif
  m_Publishing = false;

#ifdef MC_NEXCONTROL_ENABLED
  if (m_nexcontrolBinding) {
    m_nexcontrolBinding->notifyUserEdit(paramName.c_str());
  }
#endif
}

void MCVectorPlugin::beginEdit(void) {
  m_Notices.waitForCheck(mc::kCheckWaitBudgetMs);

  m_Publishing = true;
  m_License.refreshIfStale();
  applyLicenseGate();
  publishNotice();
#ifdef MC_NEXKEY_DIAGNOSTICS
  publishLicenseReport();
#endif
  m_Publishing = false;
}

void MCVectorPlugin::render(const OFX::RenderArguments &args) {
  if ((m_DstClip->getPixelDepth() == OFX::eBitDepthFloat) &&
      (m_DstClip->getPixelComponents() == OFX::ePixelComponentRGBA)) {
    MCVectorProcessor processor(*this);
    setupAndProcess(processor, args);
  } else {
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
  }
}

bool MCVectorPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                OFX::Clip *&identityClip,
                                double &identityTime) {
  if (!m_License.allowed()) {
    identityClip = m_SrcClip;
    identityTime = args.time;
    return true;
  }

  const MCVectorParams p = getActiveParams(args.time);
  if (mcvector::vectorIsIdentity(p)) {
    identityClip = m_SrcClip;
    identityTime = args.time;
    return true;
  }
  return false;
}

void MCVectorPlugin::setupAndProcess(MCVectorProcessor &processor,
                                     const OFX::RenderArguments &args) {
  std::unique_ptr<OFX::Image> dst(m_DstClip->fetchImage(args.time));
  std::unique_ptr<OFX::Image> src(m_SrcClip->fetchImage(args.time));

  if (!dst || !src) {
    OFX::throwSuiteStatusException(kOfxStatFailed);
  }

  if ((src->getPixelDepth() != dst->getPixelDepth()) ||
      (src->getPixelComponents() != dst->getPixelComponents())) {
    OFX::throwSuiteStatusException(kOfxStatErrValue);
  }

  const OfxRectI &srcBounds = src->getBounds();
  const OfxRectI &dstBounds = dst->getBounds();
  OfxRectI renderWindow;
  renderWindow.x1 =
      std::max(args.renderWindow.x1, std::max(srcBounds.x1, dstBounds.x1));
  renderWindow.y1 =
      std::max(args.renderWindow.y1, std::max(srcBounds.y1, dstBounds.y1));
  renderWindow.x2 =
      std::min(args.renderWindow.x2, std::min(srcBounds.x2, dstBounds.x2));
  renderWindow.y2 =
      std::min(args.renderWindow.y2, std::min(srcBounds.y2, dstBounds.y2));
  if (renderWindow.x1 >= renderWindow.x2 ||
      renderWindow.y1 >= renderWindow.y2) {
    return;
  }

  const MCVectorParams params =
      m_License.allowed() ? getActiveParams(args.time) : neutralParams();

  processor.setDstImg(dst.get());
  processor.setSrcImg(src.get());
  processor.setGPURenderArgs(args);
  processor.setRenderWindow(renderWindow);
  processor.setParams(params);
  processor.process();
}

MCVectorFactory::MCVectorFactory()
    : OFX::PluginFactoryHelper<MCVectorFactory>(
          kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor) {}

void MCVectorFactory::unload() {
#ifdef MC_NEXCONTROL_ENABLED
  mc::nexcontrol::shutdown();
#endif
}

static OFX::DoubleParamDescriptor *
defineDouble(OFX::ImageEffectDescriptor &desc, const char *name,
             const char *label, double def, double min, double max,
             double displayMin, double displayMax, OFX::GroupParamDescriptor &grp,
             OFX::PageParamDescriptor &page) {
  OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(name);
  param->setLabels(label, label, label);
  param->setDefault(def);
  param->setRange(min, max);
  param->setDisplayRange(displayMin, displayMax);
  param->setIncrement(0.01);
  param->setDigits(3);
  param->setDoubleType(OFX::eDoubleTypePlain);
  param->setParent(grp);
  page.addChild(*param);
  return param;
}

void MCVectorFactory::describe(OFX::ImageEffectDescriptor &desc) {
  desc.setLabels(kPluginNameLabel, kPluginNameLabel, kPluginNameLabel);
  desc.setPluginGrouping(kPluginGrouping);
  desc.setPluginDescription(kPluginDescription);

  desc.addSupportedContext(OFX::eContextFilter);
  desc.addSupportedContext(OFX::eContextGeneral);
  desc.addSupportedBitDepth(OFX::eBitDepthFloat);

  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
  desc.setSupportsMultiResolution(kSupportsMultiResolution);
  desc.setSupportsTiles(kSupportsTiles);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);

#ifdef __APPLE__
  desc.setSupportsMetalRender(true);
#else
  desc.setSupportsCudaRender(true);
  desc.setSupportsCudaStream(true);
#endif
}

void MCVectorFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        OFX::ContextEnum) {
  OFX::ClipDescriptor *srcClip =
      desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(kSupportsTiles);
  srcClip->setIsMask(false);

  OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
  dstClip->setSupportsTiles(kSupportsTiles);

  OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

  {
    OFX::GroupParamDescriptor *grp = desc.defineGroupParam("grpNotice");
    grp->setLabels("Latest Release", "Latest Release", "Latest Release");
    grp->setOpen(true);
    grp->setIsSecret(true);
    page->addChild(*grp);

    OFX::StringParamDescriptor *text =
        desc.defineStringParam(kParamNoticeText);
    text->setLabels("", "", "");
    text->setStringType(OFX::eStringTypeMultiLine);
    text->setEnabled(false);
    text->setIsPersistant(false);
    text->setParent(*grp);
    page->addChild(*text);

    OFX::PushButtonParamDescriptor *update =
        desc.definePushButtonParam(kParamNoticeUpdate);
    update->setLabels("Update", "Update", "Update");
    update->setIsSecret(true);
    update->setParent(*grp);
    page->addChild(*update);

    OFX::PushButtonParamDescriptor *openNexus =
        desc.definePushButtonParam(kParamNoticeOpenNexus);
    openNexus->setLabels("Open MCNexus", "Open MCNexus", "Open MCNexus");
    openNexus->setIsSecret(true);
    openNexus->setParent(*grp);
    page->addChild(*openNexus);

    page->addChild(OFX::PageParamDescriptor::gSkipRow);
  }

  OFX::ChoiceParamDescriptor *pivotPreset =
      desc.defineChoiceParam(kParamPivotPreset);
  pivotPreset->setLabels("Input Space", "Input Space", "Input Space");
  pivotPreset->appendOption("ACES AP1 / ACEScct");
  pivotPreset->appendOption("DaVinci Wide Gamut / Intermediate");
  pivotPreset->appendOption("ARRI Wide Gamut 3 / LogC3");
  pivotPreset->appendOption("ARRI Wide Gamut 4 / LogC4");
  pivotPreset->setDefault(1);
  page->addChild(*pivotPreset);

  OFX::ChoiceParamDescriptor *saturationModelSpace =
      desc.defineChoiceParam(kParamSaturationModelSpace);
  saturationModelSpace->setLabels("Model/Space Type", "Model/Space Type",
                                  "Model/Space Type");
  saturationModelSpace->appendOption("RGB Direct");
  saturationModelSpace->appendOption("RGB Spherical");
  saturationModelSpace->appendOption("OKLCH");
  saturationModelSpace->setDefault(1);
  page->addChild(*saturationModelSpace);

  {
    OFX::GroupParamDescriptor *grp = desc.defineGroupParam("grpSplitTone");
    grp->setLabels("Split Tone", "Split Tone", "Split Tone");
    grp->setOpen(true);
    page->addChild(*grp);

    OFX::BooleanParamDescriptor *enable =
        desc.defineBooleanParam(kParamEnableSplitTone);
    enable->setLabels("Enable Split", "Enable Split", "Enable Split");
    enable->setDefault(true);
    enable->setParent(*grp);
    page->addChild(*enable);

    defineDouble(desc, kParamSplitShadow, "Split Shadow Strength", 0.0, 0.0,
                 1.0, 0.0, 1.0, *grp, *page);
    defineDouble(desc, kParamShadowMix, "Shadow Color", 0.0, -1.0, 1.0, -1.0,
                 1.0, *grp, *page);
    defineDouble(desc, kParamNeutralBlack, "Neutral Black", 1.0, 0.0, 1.0,
                 0.0, 1.0, *grp, *page);
    defineDouble(desc, kParamSplitHighlight, "Split Highlight Strength", 0.0,
                 0.0, 1.0, 0.0, 1.0, *grp, *page);
    defineDouble(desc, kParamHighlightMix, "Highlight Color", 0.0, -1.0, 1.0,
                 -1.0, 1.0, *grp, *page);
    defineDouble(desc, kParamNeutralWhite, "Neutral White", 1.0, 0.0, 1.0,
                 0.0, 1.0, *grp, *page);
    defineDouble(desc, kParamPivotWidth, "Pivot Width", 0.0, 0.0, 1.0, 0.0,
                 1.0, *grp, *page);
    defineDouble(desc, kParamPivotOffset, "Pivot Offset", 0.0, -1.0, 1.0,
                 -1.0, 1.0, *grp, *page);
    defineDouble(desc, kParamShadowCurveBias, "Shadow Curve Bias", 0.0, -1.0,
                 1.0, -1.0, 1.0, *grp, *page);
    defineDouble(desc, kParamHighlightCurveBias, "Highlight Curve Bias", 0.0,
                 -1.0, 1.0, -1.0, 1.0, *grp, *page);
    OFX::DoubleParamDescriptor *redBellyCenter = defineDouble(
        desc, kParamShadowRedBellyCenter, "Shadow Red Belly Center",
        kShadowRedBellyCenterFixed, -1.0, 1.0, -1.0, 1.0, *grp, *page);
    redBellyCenter->setIsSecret(true);
    OFX::DoubleParamDescriptor *greenBellyCenter = defineDouble(
        desc, kParamShadowGreenBellyCenter, "Shadow Green Belly Center",
        kShadowGreenBellyCenterFixed, -1.0, 1.0, -1.0, 1.0, *grp, *page);
    greenBellyCenter->setIsSecret(true);
    OFX::DoubleParamDescriptor *blueBellyCenter = defineDouble(
        desc, kParamShadowBlueBellyCenter, "Shadow Blue Belly Center",
        kShadowBlueBellyCenterFixed, -1.0, 1.0, -1.0, 1.0, *grp, *page);
    blueBellyCenter->setIsSecret(true);

    OFX::BooleanParamDescriptor *showCurve =
        desc.defineBooleanParam(kParamShowToneCurve);
    showCurve->setLabels("Show Split Curve", "Show Split Curve",
                         "Show Split Curve");
    showCurve->setDefault(false);
    showCurve->setParent(*grp);
    page->addChild(*showCurve);
  }

  {
    OFX::GroupParamDescriptor *grp = desc.defineGroupParam("grpSaturation");
    grp->setLabels("Curves Saturation", "Curves Saturation",
                   "Curves Saturation");
    grp->setOpen(true);
    page->addChild(*grp);

    OFX::BooleanParamDescriptor *enable =
        desc.defineBooleanParam(kParamEnableSaturation);
    enable->setLabels("Enable Sat", "Enable Sat", "Enable Sat");
    enable->setDefault(true);
    enable->setParent(*grp);
    page->addChild(*enable);

    defineDouble(desc, kParamSatLow, "Low Sat", 1.0, 0.0, 2.0, 0.0, 2.0,
                 *grp, *page);
    defineDouble(desc, kParamSatMid, "Mid Sat", 1.0, 0.0, 2.0, 0.0, 2.0,
                 *grp, *page);
    defineDouble(desc, kParamSatHigh, "Hi Sat", 1.0, 0.0, 2.0, 0.0, 2.0,
                 *grp, *page);
    defineDouble(desc, kParamSatGlobal, "Global Sat", 1.0, 0.0, 2.0, 0.0,
                 2.0, *grp, *page);
    defineDouble(desc, kParamSatLumMask, "Curve Amount", 1.0, 0.0, 1.0, 0.0,
                 1.0, *grp, *page);

    OFX::BooleanParamDescriptor *showCurve =
        desc.defineBooleanParam(kParamShowSatCurve);
    showCurve->setLabels("Show Sat Curve", "Show Sat Curve",
                         "Show Sat Curve");
    showCurve->setDefault(false);
    showCurve->setParent(*grp);
    page->addChild(*showCurve);
  }

  {
    OFX::GroupParamDescriptor *grp =
        desc.defineGroupParam("grpZoneSaturation");
    grp->setLabels("Zone Saturation", "Zone Saturation", "Zone Saturation");
    grp->setOpen(true);
    page->addChild(*grp);

    OFX::BooleanParamDescriptor *enable =
        desc.defineBooleanParam(kParamEnableZoneSaturation);
    enable->setLabels("Enable Zone Sat", "Enable Zone Sat",
                      "Enable Zone Sat");
    enable->setDefault(true);
    enable->setParent(*grp);
    page->addChild(*enable);

    defineDouble(desc, kParamZone, "Zone Focus", 0.0, -1.0, 1.0, -1.0, 1.0,
                 *grp, *page);
    defineDouble(desc, kParamZonePivot, "Pivot", 0.5, 0.0, 1.0, 0.0, 1.0,
                 *grp, *page);
    defineDouble(desc, kParamZonePivotWidth, "Pivot Width", 0.5, 0.0, 1.0,
                 0.0, 1.0, *grp, *page);
    defineDouble(desc, kParamZoneStrengthSat, "Sat Strength", 0.0, -1.0, 1.0,
                 -1.0, 1.0, *grp, *page);

    OFX::BooleanParamDescriptor *showCurve =
        desc.defineBooleanParam(kParamShowZoneCurve);
    showCurve->setLabels("Show Zone Curve", "Show Zone Curve",
                         "Show Zone Curve");
    showCurve->setDefault(false);
    showCurve->setParent(*grp);
    page->addChild(*showCurve);
  }

  {
    OFX::GroupParamDescriptor *grp = desc.defineGroupParam("grpSupport");
    grp->setLabels("Support", "Support", "Support");
    grp->setOpen(true);
    page->addChild(*grp);

    OFX::PushButtonParamDescriptor *aboutHelp =
        desc.definePushButtonParam(kParamAboutHelp);
    aboutHelp->setLabels("About and Help", "About and Help", "About and Help");
    aboutHelp->setParent(*grp);
    page->addChild(*aboutHelp);

    OFX::PushButtonParamDescriptor *refresh =
        desc.definePushButtonParam(kParamLicenseRefreshBtn);
    refresh->setLabels("Refresh License", "Refresh License", "Refresh License");
    refresh->setParent(*grp);
    page->addChild(*refresh);

    OFX::PushButtonParamDescriptor *appMCNexus =
        desc.definePushButtonParam(kParamAppMCNexus);
    appMCNexus->setLabels("App MCNexus", "App MCNexus", "App MCNexus");
#if !defined(__APPLE__) && !defined(_WIN32)
    appMCNexus->setEnabled(false);
#endif
    appMCNexus->setParent(*grp);
    page->addChild(*appMCNexus);
  }

#ifdef MC_NEXCONTROL_ENABLED
  {
    OFX::GroupParamDescriptor *grp = desc.defineGroupParam("grpNexControl");
    grp->setLabels("NexControl", "NexControl", "NexControl");
    grp->setOpen(false);
    page->addChild(*grp);

    OFX::StringParamDescriptor *instParam =
        desc.defineStringParam("instance_info");
    instParam->setLabels("Status", "Status", "Status");
    instParam->setStringType(OFX::eStringTypeSingleLine);
    instParam->setDefault("Node Instance (Inactive)");
    instParam->setEnabled(false);
    instParam->setIsPersistant(false);
    instParam->setParent(*grp);
    page->addChild(*instParam);
  }
#endif

#ifdef MC_NEXKEY_DIAGNOSTICS
  {
    OFX::GroupParamDescriptor *grp = desc.defineGroupParam("grpLicense");
    grp->setLabels("License", "License", "License");
    grp->setOpen(false);
    page->addChild(*grp);

    OFX::StringParamDescriptor *status =
        desc.defineStringParam(kParamLicenseStatus);
    status->setLabels("Status", "Status", "Status");
    status->setStringType(OFX::eStringTypeSingleLine);
    status->setEnabled(false);
    status->setIsPersistant(false);
    status->setParent(*grp);
    page->addChild(*status);

    OFX::StringParamDescriptor *edition =
        desc.defineStringParam(kParamLicenseEdition);
    edition->setLabels("Edition", "Edition", "Edition");
    edition->setStringType(OFX::eStringTypeSingleLine);
    edition->setEnabled(false);
    edition->setIsPersistant(false);
    edition->setParent(*grp);
    page->addChild(*edition);

    OFX::StringParamDescriptor *validity =
        desc.defineStringParam(kParamLicenseValidity);
    validity->setLabels("Validity", "Validity", "Validity");
    validity->setStringType(OFX::eStringTypeSingleLine);
    validity->setEnabled(false);
    validity->setIsPersistant(false);
    validity->setParent(*grp);
    page->addChild(*validity);

    OFX::StringParamDescriptor *activation =
        desc.defineStringParam(kParamLicenseActivation);
    activation->setLabels("Activation", "Activation", "Activation");
    activation->setStringType(OFX::eStringTypeSingleLine);
    activation->setEnabled(false);
    activation->setIsPersistant(false);
    activation->setParent(*grp);
    page->addChild(*activation);

    OFX::StringParamDescriptor *sync =
        desc.defineStringParam(kParamLicenseSync);
    sync->setLabels("Sync", "Sync", "Sync");
    sync->setStringType(OFX::eStringTypeSingleLine);
    sync->setEnabled(false);
    sync->setIsPersistant(false);
    sync->setParent(*grp);
    page->addChild(*sync);

    OFX::StringParamDescriptor *hostInfo =
        desc.defineStringParam(kParamDiagHost);
    hostInfo->setLabels("Host", "Host", "Host");
    hostInfo->setStringType(OFX::eStringTypeSingleLine);
    hostInfo->setEnabled(false);
    hostInfo->setIsPersistant(false);
    hostInfo->setParent(*grp);
    page->addChild(*hostInfo);

    OFX::StringParamDescriptor *path =
        desc.defineStringParam(kParamLicensePath);
    path->setLabels("Receipts", "Receipts", "Receipts");
    path->setStringType(OFX::eStringTypeSingleLine);
    path->setEnabled(false);
    path->setIsPersistant(false);
    path->setParent(*grp);
    page->addChild(*path);

    OFX::PushButtonParamDescriptor *syncNow =
        desc.definePushButtonParam(kParamLicenseSync "Btn");
    syncNow->setLabels("Sync Now", "Sync Now", "Sync Now");
    syncNow->setParent(*grp);
    page->addChild(*syncNow);

    OFX::PushButtonParamDescriptor *refresh =
        desc.definePushButtonParam(kParamLicenseRefresh);
    refresh->setLabels("Reload Receipt", "Reload Receipt", "Reload Receipt");
    refresh->setParent(*grp);
    page->addChild(*refresh);
  }
#endif
}

OFX::ImageEffect *MCVectorFactory::createInstance(OfxImageEffectHandle handle,
                                                  OFX::ContextEnum) {
  return new MCVectorPlugin(handle);
}

void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray &factoryArray) {
  static MCVectorFactory plugin;
  factoryArray.push_back(&plugin);
}
