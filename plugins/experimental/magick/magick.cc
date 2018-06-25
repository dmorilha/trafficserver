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

const char * const TAG = "magick";

typedef std::vector< char > CharVector;
typedef std::vector< char * > CharPointerVector;
typedef std::vector< std::string_view > StringViewVector;

struct MagickExceptionInfo {
  ExceptionInfo * info;

  ~MagickExceptionInfo() {
    assert(nullptr != info);
    info = DestroyExceptionInfo(info);
  }

  MagickExceptionInfo(void) :
    info(AcquireExceptionInfo()) {
    assert(nullptr != info);
  }
};

struct MagickImageInfo {
  ImageInfo * info;

  ~MagickImageInfo() {
    assert(nullptr != info);
    info = DestroyImageInfo(info);
  }

  MagickImageInfo(void) :
    info(AcquireImageInfo()) {
    assert(nullptr != info);
    info->temporary = MagickTrue;
  }

  void setBlob(const std::vector< char > & v) const {
    assert( ! v.empty());
    assert(nullptr != info);
    SetImageInfoBlob(info, reinterpret_cast< const void * >(v.data()), v.size());
  }
};

struct MagickCore {
  ~MagickCore() {
    MagickCoreTerminus();
  }

  MagickCore(void) {
    MagickCoreGenesis("/tmp", MagickTrue);
  }
};

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
  std::size_t s = 0;
  TSBase64Decode(v.data(), v.size(), reinterpret_cast< unsigned char * >(v.data()),
      v.size(), &s);
  v.resize(s);
  std::cout << std::string_view(v.data(), v.size()) << std::endl;
  std::size_t i = 0, j = 0;
  for (; i < v.size(); ++i) {
    char & c = v[i];
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
    //transaction.getServerResponse().getHeaders()["Content-Type"] = "image/webp";

    //TS_DEBUG(TAG, "url %s", transaction.getServerRequest().getUrl().getUrlString().c_str());

    t.resume();
  }

  void consume(const std::string_view s) override {
    blob_.insert(blob_.end(), s.begin(), s.end());
  }

  void handleInputComplete(void) override {
    MagickImageInfo image;
    MagickExceptionInfo exception;

    MagickImageCommand(image.info, argumentMap_.size(),
        argumentMap_.data(), NULL, exception.info);

    if (exception.info->severity != UndefinedException) {
      CatchException(exception.info);
    }

    //produce(std::string_view(reinterpret_cast< const char * >(""), 0));

    setOutputComplete();
  }

  CharVector arguments_;
  CharPointerVector argumentMap_;
  CharVector blob_;
};

struct GlobalHookPlugin : GlobalPlugin {
  MagickCore magickCore;

  GlobalHookPlugin(void) {
    registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void handleReadResponseHeaders(Transaction & t) override {
    std::cout << "Hello World!" << std::endl;
    const string contentType = t.getServerResponse().getHeaders().values("Content-Type");
    {
      const QueryMap queryMap(t.getServerRequest().getUrl().getQuery());
      {
        const auto & magickQueryParameter = queryMap["magick"];
        if ( ! magickQueryParameter.empty()) {
          const auto & view = magickQueryParameter.front();
          std::cout << "base64 " << view << std::endl;
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

    if (contentType.find("jpeg") != string::npos
        || contentType.find("png") != string::npos) {
      //TS_DEBUG(TAG, "Content type is either jpeg or png. Converting to webp");
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
