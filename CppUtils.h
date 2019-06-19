//
//  CppUtils.h
//  Toolbox
//
//  Created by Chris Birkhold on 8/19/18.
//  Copyright © 2018 Chris Birkhold. All rights reserved.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __TOOLBOX__CPP_UTILS_H__
#define __TOOLBOX__CPP_UTILS_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <iomanip>
#include <string>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace toolbox {

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  //------------------------------------------------------------------------------
  // Utility functions based on the C++ STL.
  //------------------------------------------------------------------------------

  class StlUtils
  {
  public:

    static std::string load_file(const std::string& path);

    template<class ValueT>
    class HexInsert {
    public:

      typedef ValueT Value;

      HexInsert(Value value, size_t width)
      : m_value(value)
      , m_width(width)
      {
      }


      template<class CharT, class TraitsT>
      friend std::basic_ostream<CharT, TraitsT>& operator<<(std::basic_ostream<CharT, TraitsT>& stream, const HexInsert& hex)
      { 
        static_assert((std::is_integral<ValueT>::value == true) || (std::is_pointer<ValueT>::value == true), "!");

        std::ios_base::fmtflags flags(stream.flags());
        
        stream << "0x" << std::hex;
        stream << std::setfill('0');
        
        if (hex.m_width == size_t(-1)) {
          constexpr const size_t max_hex_digits = (std::numeric_limits<ValueT>::digits / 4);
          stream << std::setw(max_hex_digits);
        }
        else {
          stream << std::setw(hex.m_width);
        }

        stream << hex.m_value;

        stream.flags(flags);

        return stream;
      }

    private:

      const Value     m_value;
      const size_t    m_width;
    };

    template<class ValueT>
    static HexInsert<ValueT> hex_insert(ValueT value, size_t width = size_t(-1))
    {
      return HexInsert<ValueT>(value, width);
    }
  };

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  //------------------------------------------------------------------------------
  // Utility for executing a function at the end of the current scope. For example
  // to delete a dynamically allocated object that is not designed to work with a
  // smart pointer as it goes out of scope.
  //
  // STATUS: Alpha
  //------------------------------------------------------------------------------

  template<class T>
  class AtEndOfScope
  {
  public:

    explicit AtEndOfScope(T&& callable) : m_callable(std::move(callable)) {}
    ~AtEndOfScope() { m_callable(); }

  private:

    T   m_callable;
  };

  template<class T> AtEndOfScope<T> make_at_end_of_scope(T&& callable)
  {
    return AtEndOfScope<T>(std::forward<T>(callable));
  }

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

} // namespace toolbox

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __TOOLBOX__CPP_UTILS_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
