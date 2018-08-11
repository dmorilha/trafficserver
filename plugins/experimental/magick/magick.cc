/**
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or ageed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <map>
#include <iostream>
#include <string>
#include <vector>

#include <cassert>

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/TransformationPlugin.h>

#include <ts/ts.h>

#include "MagickWand/MagickWand.h"
#include "MagickWand/magick-cli.h"

using std::string;
using namespace atscppapi;

typedef std::vector< char > CharVector;
typedef std::vector< char * > CharPointerVector;
typedef std::vector< std::string_view > StringViewVector;

namespace magick {

struct Exception {
  ExceptionInfo * info;

  ~Exception() {
    assert(nullptr != info);
    info = DestroyExceptionInfo(info);
  }

  Exception(void) :
    info(AcquireExceptionInfo()) {
    assert(nullptr != info);
  }
};

struct Image {
  ImageInfo * info;

  ~Image() {
    assert(nullptr != info);
    info = DestroyImageInfo(info);
  }

  Image(void) :
    info(AcquireImageInfo()) {
    assert(nullptr != info);
  }
};

struct Wand {
  MagickWand * wand;
  void * blob;

  ~Wand() {
    assert(nullptr != wand);
    wand = DestroyMagickWand(wand);
    if (nullptr == blob) {
      blob = MagickRelinquishMemory(blob);
    }
  }

  Wand(void) :
    wand(NewMagickWand()), blob(nullptr)  {
    assert(nullptr != wand);
  }

  void clear(void) const {
    assert(nullptr != wand);
    ClearMagickWand(wand);
  }

  std::string_view get(void) {
    assert(nullptr != wand);
    std::size_t length = 0;
    if (nullptr != blob) {
      blob = MagickRelinquishMemory(blob);
    }
    MagickResetIterator(wand);
    blob = MagickGetImagesBlob(wand, &length);
    return std::string_view(reinterpret_cast< char * >(blob), length);
  }

  bool read(const char * const s) const {
    assert(nullptr != s);
    assert(nullptr != wand);
    return MagickReadImage(wand, s) == MagickTrue;
  }

  bool readBlob(const std::vector< char > & v) const {
    assert( ! v.empty());
    assert(nullptr != wand);
    return MagickReadImageBlob(wand, v.data(), v.size()) == MagickTrue;
  }

  bool setFormat(const char * const s) const {
    assert(nullptr != s);
    assert(nullptr != wand);
    return MagickSetImageFormat(wand, s) == MagickTrue;
  }

  bool write(const char * const s) const {
    assert(nullptr != s);
    assert(nullptr != wand);
    return MagickWriteImage(wand, s) == MagickTrue;
  }
};

struct Core {
  ~Core() {
    MagickCoreTerminus();
  }

  Core(void) {
    MagickCoreGenesis("/tmp", MagickFalse);
  }
};

} //end of magick namespace

struct QueryMap {
  typedef StringViewVector Vector;
  typedef std::map< std::string_view, Vector > Map;

  const static Vector emptyValues;

  std::string content_;
  Map map_;

  QueryMap(std::string && s) : content_(s) {
    parse();
  }

  template < typename T >
  const Vector & operator [] (T && k) const {
    const auto iterator = map_.find(k);
    if (iterator != map_.end()) {
      return iterator->second;
    }
    return emptyValues;
  }

  void parse(void) {
    std::string_view key;
    std::size_t i = 0, j = 0;
    for (; i < content_.size(); ++i) {
      const char c = content_[i];
      switch (c) {
      case '&':
        if ( ! key.empty()) {
          map_[key].emplace_back(std::string_view(&content_[j],
                i - j));
          key = std::string_view();
        }
        j = i + 1;
        break;
      case '=':
        key = std::string_view(&content_[j], i - j);
        j = i + 1;
        break;
      default:
        break;
      }
    }

    assert(j <= i);

    if (key.empty()) {
      if (j < i) {
        map_[std::string_view(&content_[j], i - j)];
      }

    } else {
      map_[key].emplace_back(std::string_view(&content_[j],
            i - j));
    }
  }
};

const QueryMap::Vector QueryMap::emptyValues;

CharPointerVector QueryParameterToArguments(CharVector & v) {
  CharPointerVector result;
  result.reserve(32);

  for (auto & c : v) {
    switch (c) {
    case '.':
      c = '+';
      break;
    case '_':
      c = '/';
      break;
    case '-':
      c = '=';
      break;
    default:
      break;
    }
  }

  std::size_t s = 0;
  TSBase64Decode(v.data(), v.size(), reinterpret_cast< unsigned char * >(v.data()), v.size(), &s);

  v.resize(s);

  std::size_t i = 0, j = 0;
  for (; i < v.size(); ++i) {
    char & c = v[i];
    assert('\0' != c);
    if (' ' == c) {
      if (i > j) {
        result.push_back(&v[j]);
      }
      c = '\0';
      j = i + 1;
    }
  }
  if (i > j) {
    result.push_back(&v[j]);
  }
  return result;
}

struct ImageTransform : TransformationPlugin {

  ~ImageTransform() override { }

  ImageTransform(Transaction & t, CharVector && a, CharPointerVector && m) :
    TransformationPlugin(t, TransformationPlugin::RESPONSE_TRANSFORMATION),
    arguments_(std::move(a)), argumentMap_(std::move(m)) {
    TransformationPlugin::registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void handleReadResponseHeaders(Transaction & t) override {
    //TODO(daniel): content-type has to be extracted from ImageInfo?
    t.resume();
  }

  void consume(const std::string_view s) override {
    blob_.insert(blob_.end(), s.begin(), s.end());
  }

  void handleInputComplete(void) override {
    magick::Image image;
    magick::Exception exception;
    magick::Wand wand;

    wand.readBlob(blob_);
    wand.write("mpr:b");

    bool result = MagickCommandGenesis(image.info, ConvertImageCommand,
      argumentMap_.size(), argumentMap_.data(), NULL, exception.info) == MagickTrue;


    if (exception.info->severity != UndefinedException) {
      //CatchException(exception.info);
    }

    wand.clear();
    wand.read("mpr:a");
    //wand.setFormat("jpeg");

    const std::string_view output = wand.get();
    produce(output);

    setOutputComplete();
  }

  CharVector arguments_;
  CharPointerVector argumentMap_;
  CharVector blob_;
};

struct GlobalHookPlugin : GlobalPlugin {
  magick::Core core;

  GlobalHookPlugin(void) {
    registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void handleReadResponseHeaders(Transaction & t) override {
    const string contentType = t.getServerResponse().getHeaders().values("Content-Type");
    {
      const QueryMap queryMap(t.getServerRequest().getUrl().getQuery());
      {
        const auto & magickQueryParameter = queryMap["magick"];
        if ( ! magickQueryParameter.empty()) {
          const auto & view = magickQueryParameter.front();
          CharVector magick(view.data(), view.data() + view.size());
          if (true) {
            const auto & magickSigQueryParameter = queryMap["magickSig"];
            if ( ! magickQueryParameter.empty()) {
              const auto & magickSig = magickSigQueryParameter.front();
            }
          }

          CharPointerVector argumentMap = QueryParameterToArguments(magick);
          t.addPlugin(new ImageTransform(t,
                std::move(magick), std::move(argumentMap)));
        }
      }
    }

    t.resume();
  }
};

void TSPluginInit(int, const char * *) {
  if ( ! RegisterGlobalPlugin("magick", "apache", "dmorilha@gmail.com")) {
    return;
  }

  //TODO(daniel): LEAK!!!
  new GlobalHookPlugin();
}
