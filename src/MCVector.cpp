// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#include "MCVector.h"

#include "VectorMath.h"
#include "VectorParams.h"

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
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"

#ifndef PLUGIN_VERSION
#define kPluginVersion "v0.0.1"
#else
#define kPluginVersion PLUGIN_VERSION
#endif

#define kPluginName "Vector"
#define kPluginNameLabel "Vector " kPluginVersion
#define kPluginGrouping "MC Plugins"
#define kPluginDescription                                                     \
  "Parallel RGB curves saturation and split tone processor. The Input Space "  \
  "control is used only as a split-tone pivot preset."
#define kPluginIdentifier "com.MCVector"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

#define kSupportsTiles false
#define kSupportsMultiResolution false
#define kSupportsMultipleClipPARs false

#define kParamPivotPreset "pivotPreset"
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

#define kAboutHelpUrl "https://github.com/ciqueira/Vector"

static constexpr double kShadowRedBellyCenterFixed = -0.705;
static constexpr double kShadowGreenBellyCenterFixed = -0.556;
static constexpr double kShadowBlueBellyCenterFixed = -0.626;

static void openExternalUrl(const char *url) {
#ifdef _WIN32
  ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::string command = "open \"";
  command += url;
  command += "\" >/dev/null 2>&1";
  std::system(command.c_str());
#else
  std::string command = "xdg-open \"";
  command += url;
  command += "\" >/dev/null 2>&1 &";
  std::system(command.c_str());
#endif
}

static void openMCNexusApp() {
#ifdef __APPLE__
  std::system(
      "open -a MCNexus >/dev/null 2>&1 || open \"/Applications/MCNexus.app\" "
      ">/dev/null 2>&1");
#elif defined(_WIN32)
  auto shellExecuteWindowsPath = [](const wchar_t *path,
                                    const wchar_t *parameters) {
    HINSTANCE result =
        ShellExecuteW(nullptr, L"open", path, parameters, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
  };

  auto launchWindowsExecutableIfExists = [&](const wchar_t *pathWithEnvironment) {
    wchar_t expanded[MAX_PATH] = {};
    const DWORD expandedLength =
        ExpandEnvironmentStringsW(pathWithEnvironment, expanded, MAX_PATH);
    const wchar_t *path =
        (expandedLength > 0 && expandedLength < MAX_PATH) ? expanded
                                                          : pathWithEnvironment;
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
      return false;
    }
    return shellExecuteWindowsPath(path, nullptr);
  };

  auto launchPowerShellHidden = [](const wchar_t *parameters) {
    std::wstring commandLine = L"powershell.exe ";
    commandLine += parameters;

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo = {};
    const BOOL created =
        CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo,
                       &processInfo);
    if (!created) {
      return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
  };

  if (launchWindowsExecutableIfExists(L"%ProgramFiles%\\MCNexus\\MCNexus.exe") ||
      launchWindowsExecutableIfExists(
          L"%ProgramFiles(x86)%\\MCNexus\\MCNexus.exe") ||
      launchWindowsExecutableIfExists(
          L"%LocalAppData%\\Programs\\MCNexus\\MCNexus.exe")) {
    return;
  }

  constexpr const wchar_t *kPowerShellArgs =
      LR"PS(-NoProfile -WindowStyle Hidden -Command "$app = Get-StartApps | Where-Object { $_.Name -eq 'MCNexus' } | Select-Object -First 1; if ($app) { Start-Process ('shell:AppsFolder\' + $app.AppID) } else { Start-Process 'https://apps.microsoft.com/detail/9n1qqt1xc825?hl=en-US&gl=US' }")PS";
  if (launchPowerShellHidden(kPowerShellArgs)) {
    return;
  }

  openExternalUrl("https://apps.microsoft.com/detail/9n1qqt1xc825?hl=en-US&gl=US");
#endif
}

#ifdef __APPLE__
extern "C" void RunMetalKernel(void *commandQueue, int width, int height,
                               int rowBytes, const float *input,
                               float *output, MCVectorParams params);
#else
extern "C" void RunCudaKernel(void *stream, int width, int height,
                              int rowBytes, const float *input, float *output,
                              MCVectorParams params);
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
    const OfxRectI &bounds = _srcImg->getBounds();
    const int width = bounds.x2 - bounds.x1;
    const int height = bounds.y2 - bounds.y1;
    const int rowBytes = _srcImg->getRowBytes();
    const float *input = static_cast<const float *>(_srcImg->getPixelData());
    float *output = static_cast<float *>(_dstImg->getPixelData());

    RunCudaKernel(_pCudaStream, width, height, rowBytes, input, output,
                  _params);
#endif
  }

  virtual void processImagesMetal() override {
#ifdef __APPLE__
    const OfxRectI &bounds = _srcImg->getBounds();
    const int width = bounds.x2 - bounds.x1;
    const int height = bounds.y2 - bounds.y1;
    const int rowBytes = _srcImg->getRowBytes();
    const float *input = static_cast<const float *>(_srcImg->getPixelData());
    float *output = static_cast<float *>(_dstImg->getPixelData());

    RunMetalKernel(_pMetalCmdQ, width, height, rowBytes, input, output,
                   _params);
#endif
  }

  virtual void multiThreadProcessImages(OfxRectI procWindow) override {
    using namespace mcvector;

    const OfxRectI &bounds = _srcImg->getBounds();
    const int width = bounds.x2 - bounds.x1;
    const int height = bounds.y2 - bounds.y1;

    for (int y = procWindow.y1; y < procWindow.y2; ++y) {
      if (_effect.abort()) {
        break;
      }

      const float *srcRow =
          static_cast<const float *>(_srcImg->getPixelAddress(procWindow.x1, y));
      float *dstRow =
          static_cast<float *>(_dstImg->getPixelAddress(procWindow.x1, y));
      if (!srcRow || !dstRow) {
        continue;
      }

      const int rowWidth = procWindow.x2 - procWindow.x1;
      for (int x = 0; x < rowWidth; ++x) {
        const int imageX = procWindow.x1 + x - bounds.x1;
        const int imageY = y - bounds.y1;
        const float3 in =
            make_float3(srcRow[x * 4 + 0], srcRow[x * 4 + 1],
                        srcRow[x * 4 + 2]);

        float3 out = applyVector(in, _params);
        out = drawToneCurve(out, imageX, imageY, width, height, _params);
        out = drawSatCurve(out, imageX, imageY, width, height, _params);
        out = drawZoneCurve(out, imageX, imageY, width, height, _params);

        dstRow[x * 4 + 0] = out.x;
        dstRow[x * 4 + 1] = out.y;
        dstRow[x * 4 + 2] = out.z;
        dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
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

  virtual void render(const OFX::RenderArguments &args) override;
  virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                          OFX::Clip *&identityClip,
                          double &identityTime) override;
  virtual void changedParam(const OFX::InstanceChangedArgs &args,
                            const std::string &paramName) override;

private:
  MCVectorParams getActiveParams(double time);
  void setupAndProcess(MCVectorProcessor &processor,
                       const OFX::RenderArguments &args);

  OFX::Clip *m_SrcClip;
  OFX::Clip *m_DstClip;

  OFX::ChoiceParam *m_PivotPreset;
  OFX::BooleanParam *m_EnableSaturation;
  OFX::DoubleParam *m_SatLow;
  OFX::DoubleParam *m_SatMid;
  OFX::DoubleParam *m_SatHigh;
  OFX::DoubleParam *m_SatGlobal;
  OFX::DoubleParam *m_SatLumMask;
  OFX::BooleanParam *m_ShowSatCurve;
  OFX::BooleanParam *m_EnableZoneSaturation;
  OFX::DoubleParam *m_Zone;
  OFX::DoubleParam *m_ZonePivot;
  OFX::DoubleParam *m_ZonePivotWidth;
  OFX::DoubleParam *m_ZoneStrengthSat;
  OFX::BooleanParam *m_ShowZoneCurve;
  OFX::BooleanParam *m_EnableSplitTone;
  OFX::DoubleParam *m_SplitShadow;
  OFX::DoubleParam *m_ShadowMix;
  OFX::DoubleParam *m_NeutralBlack;
  OFX::DoubleParam *m_SplitHighlight;
  OFX::DoubleParam *m_HighlightMix;
  OFX::DoubleParam *m_NeutralWhite;
  OFX::DoubleParam *m_PivotWidth;
  OFX::DoubleParam *m_PivotOffset;
  OFX::DoubleParam *m_ShadowCurveBias;
  OFX::DoubleParam *m_HighlightCurveBias;
  OFX::DoubleParam *m_ShadowRedBellyCenter;
  OFX::DoubleParam *m_ShadowGreenBellyCenter;
  OFX::DoubleParam *m_ShadowBlueBellyCenter;
  OFX::BooleanParam *m_ShowToneCurve;
};

MCVectorPlugin::MCVectorPlugin(OfxImageEffectHandle handle)
    : OFX::ImageEffect(handle) {
  m_DstClip = fetchClip(kOfxImageEffectOutputClipName);
  m_SrcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

  m_PivotPreset = fetchChoiceParam(kParamPivotPreset);
  m_EnableSaturation = fetchBooleanParam(kParamEnableSaturation);
  m_SatLow = fetchDoubleParam(kParamSatLow);
  m_SatMid = fetchDoubleParam(kParamSatMid);
  m_SatHigh = fetchDoubleParam(kParamSatHigh);
  m_SatGlobal = fetchDoubleParam(kParamSatGlobal);
  m_SatLumMask = fetchDoubleParam(kParamSatLumMask);
  m_ShowSatCurve = fetchBooleanParam(kParamShowSatCurve);
  m_EnableZoneSaturation = fetchBooleanParam(kParamEnableZoneSaturation);
  m_Zone = fetchDoubleParam(kParamZone);
  m_ZonePivot = fetchDoubleParam(kParamZonePivot);
  m_ZonePivotWidth = fetchDoubleParam(kParamZonePivotWidth);
  m_ZoneStrengthSat = fetchDoubleParam(kParamZoneStrengthSat);
  m_ShowZoneCurve = fetchBooleanParam(kParamShowZoneCurve);
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
}

MCVectorParams MCVectorPlugin::getActiveParams(double time) {
  MCVectorParams p;
  std::memset(&p, 0, sizeof(p));

  int pivotPreset = 1;
  m_PivotPreset->getValueAtTime(time, pivotPreset);
  p.pivotPreset = pivotPreset;
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

void MCVectorPlugin::changedParam(const OFX::InstanceChangedArgs &,
                                  const std::string &paramName) {
  if (paramName == kParamAboutHelp) {
    openExternalUrl(kAboutHelpUrl);
  } else if (paramName == kParamAppMCNexus) {
    openMCNexusApp();
  }
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
  const MCVectorParams p = getActiveParams(args.time);
  const bool satIdentity =
      !p.enableSaturation ||
      (p.satLow == 1.0f && p.satMid == 1.0f && p.satHigh == 1.0f &&
       p.satGlobal == 1.0f);
  const bool zoneSatIdentity =
      !p.enableZoneSaturation ||
      p.zoneHighlightSaturation == 0.0f;
  const bool splitIdentity =
      !p.enableSplitTone ||
      (p.splitShadow == 0.0f && p.splitHighlight == 0.0f);
  const bool noOverlays =
      !(p.enableSaturation && p.showSatCurve) &&
      !(p.enableSplitTone && p.showToneCurve) &&
      !(p.enableZoneSaturation && p.showZoneCurve);
  if (satIdentity && zoneSatIdentity && splitIdentity && noOverlays) {
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

  processor.setDstImg(dst.get());
  processor.setSrcImg(src.get());
  processor.setGPURenderArgs(args);
  processor.setRenderWindow(args.renderWindow);
  processor.setParams(getActiveParams(args.time));
  processor.process();
}

MCVectorFactory::MCVectorFactory()
    : OFX::PluginFactoryHelper<MCVectorFactory>(
          kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor) {}

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

  OFX::ChoiceParamDescriptor *pivotPreset =
      desc.defineChoiceParam(kParamPivotPreset);
  pivotPreset->setLabels("Input Space", "Input Space", "Input Space");
  pivotPreset->appendOption("ACES AP1 / ACEScct");
  pivotPreset->appendOption("DaVinci Wide Gamut / Intermediate");
  pivotPreset->appendOption("ARRI Wide Gamut 3 / LogC3");
  pivotPreset->appendOption("ARRI Wide Gamut 4 / LogC4");
  pivotPreset->setDefault(1);
  page->addChild(*pivotPreset);

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
    defineDouble(desc, kParamSatLumMask, "Luma Mask", 1.0, 0.0, 1.0, 0.0,
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

    defineDouble(desc, kParamZone, "Zone", 0.0, -1.0, 1.0, -1.0, 1.0, *grp,
                 *page);
    defineDouble(desc, kParamZonePivot, "Pivot", 0.5, 0.0, 1.0, 0.0, 1.0,
                 *grp, *page);
    defineDouble(desc, kParamZonePivotWidth, "Pivot Width", 0.5, 0.0, 1.0,
                 0.0, 1.0, *grp, *page);
    defineDouble(desc, kParamZoneStrengthSat, "Strength Sat", 0.0, -1.0, 1.0,
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
    grp->setOpen(false);
    page->addChild(*grp);

    OFX::PushButtonParamDescriptor *aboutHelp =
        desc.definePushButtonParam(kParamAboutHelp);
    aboutHelp->setLabels("About and Help", "About and Help", "About and Help");
    aboutHelp->setParent(*grp);
    page->addChild(*aboutHelp);

    OFX::PushButtonParamDescriptor *appMCNexus =
        desc.definePushButtonParam(kParamAppMCNexus);
    appMCNexus->setLabels("App MCNexus", "App MCNexus", "App MCNexus");
#if !defined(__APPLE__) && !defined(_WIN32)
    appMCNexus->setEnabled(false);
#endif
    appMCNexus->setParent(*grp);
    page->addChild(*appMCNexus);
  }
}

OFX::ImageEffect *MCVectorFactory::createInstance(OfxImageEffectHandle handle,
                                                  OFX::ContextEnum) {
  return new MCVectorPlugin(handle);
}

void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray &factoryArray) {
  static MCVectorFactory plugin;
  factoryArray.push_back(&plugin);
}
