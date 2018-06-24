/**
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <sstream>
#include <iostream>
#include <string_view>
#include <atscppapi/PluginInit.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/Logger.h>

#include <MagickWand/convert.h>

using std::string;
using namespace Magick;
using namespace atscppapi;

#define TAG "webp_transform"

class ImageTransform : public TransformationPlugin
{
public:
  ImageTransform(Transaction &transaction) : TransformationPlugin(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION)
  {
    TransformationPlugin::registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    transaction.getServerResponse().getHeaders()["Content-Type"] = "image/webp";
    transaction.getServerResponse().getHeaders()["Vary"]         = "Content-Type"; // to have a separate cache entry.

    TS_DEBUG(TAG, "url %s", transaction.getServerRequest().getUrl().getUrlString().c_str());
    transaction.resume();
  }

  void
  consume(std::string_view data) override
  {
    _img.write(data.data(), data.length());
  }

  void
  handleInputComplete() override
  {
    string input_data = _img.str();
    Blob input_blob(input_data.data(), input_data.length());
    Image image;
    image.read(input_blob);

    Blob output_blob;
    image.magick("WEBP");
    image.write(&output_blob);
    produce(std::string_view(reinterpret_cast<const char *>(output_blob.data()), output_blob.length()));

    setOutputComplete();
  }

  ~ImageTransform() override {}

private:
  std::stringstream _img;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_RESPONSE_HEADERS); }
  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    string ctype      = transaction.getServerResponse().getHeaders().values("Content-Type");
    string user_agent = transaction.getServerRequest().getHeaders().values("User-Agent");
    if (user_agent.find("Chrome") != string::npos && (ctype.find("jpeg") != string::npos || ctype.find("png") != string::npos)) {
      TS_DEBUG(TAG, "Content type is either jpeg or png. Converting to webp");
      transaction.addPlugin(new ImageTransform(transaction));
    }

    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Webp_Transform", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  InitializeMagick("");
  new GlobalHookPlugin();
}

/**
 * content of api_examples/magick_command.c
 */

/*
   Direct call to MagickImageCommand(),
   which is basically what the "magick" command does via
   a wrapper function MagickCommandGenesis()

   Compile with ImageMagick-devlop installed...

     gcc -lMagickWand -lMagickCore magick_command.c -o magick_command

   Compile and run directly from Source Directory...

     IM_PROG=api_examples/magick_command
     gcc -I`pwd` -LMagickWand/.libs -LMagickCore/.libs \
       -lMagickWand -lMagickCore  $IM_PROG.c -o $IM_PROG

     sh ./magick.sh $IM_PROG

*/
#include <stdio.h>
#include "MagickCore/studio.h"
#include "MagickCore/exception.h"
#include "MagickCore/exception-private.h"
#include "MagickCore/image.h"
#include "MagickWand/MagickWand.h"
#include "MagickWand/magick-cli.h"

int main(int argc, char **argv)
{
  MagickCoreGenesis(argv[0],MagickFalse);

  {
    
    ImageInfo *image_info = AcquireImageInfo();
    ExceptionInfo *exception = AcquireExceptionInfo();

    int arg_count;
    char *args[] = { "magick", "-size", "100x100", "xc:red",
                     "(", "rose:", "-rotate", "-90", ")",
                     "+append", "show:", NULL };

    for(arg_count = 0; args[arg_count] != (char *) NULL; arg_count++);

    (void) MagickImageCommand(image_info, arg_count, args, NULL, exception);

    if (exception->severity != UndefinedException)
    {
      CatchException(exception);
      fprintf(stderr, "Major Error Detected\n");
    }

    image_info=DestroyImageInfo(image_info);
    exception=DestroyExceptionInfo(exception);
  }
  MagickCoreTerminus();
}
