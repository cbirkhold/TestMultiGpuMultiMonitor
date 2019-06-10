//
//  CppUtilities.cpp
//  Toolbox
//
//  Created by Chris Birkhold on 8/19/18.
//  Copyright © 2018 Chris Birkhold. All rights reserved.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CppUtilities.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <fstream>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace toolbox {

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  std::string
  StlUtils::load_file(const std::string& path)
  {
    std::ifstream stream(path, (std::ios::binary | std::ios::ate));

    if (!stream.is_open()) {
      throw std::runtime_error("Failed to load file!");
    }

    const size_t length = stream.tellg();
    stream.seekg(0, std::ios::beg);

    std::string content(length, '\0');
    stream.read(&content[0], length);

    return content;
  }

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

} // namespace toolbox

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
