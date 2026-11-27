// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#pragma once

#include "ofxsImageEffect.h"

class MCVectorFactory : public OFX::PluginFactoryHelper<MCVectorFactory> {
public:
  MCVectorFactory();
  virtual void load() override {}
  virtual void unload() override;
  virtual void describe(OFX::ImageEffectDescriptor &desc) override;
  virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                 OFX::ContextEnum context) override;
  virtual OFX::ImageEffect *createInstance(OfxImageEffectHandle handle,
                                           OFX::ContextEnum context) override;
};
