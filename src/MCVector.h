// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#pragma once

#include "ofxsImageEffect.h"

class MCVectorFactory : public OFX::PluginFactoryHelper<MCVectorFactory> {
public:
  MCVectorFactory();
  virtual void load() {}
  virtual void unload() {}
  virtual void describe(OFX::ImageEffectDescriptor &desc);
  virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                 OFX::ContextEnum context);
  virtual OFX::ImageEffect *createInstance(OfxImageEffectHandle handle,
                                           OFX::ContextEnum context);
};
