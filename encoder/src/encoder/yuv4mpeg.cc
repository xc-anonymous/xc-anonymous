/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "yuv4mpeg.hh"

#include <fcntl.h>
#include <sstream>
#include <utility>

using namespace std;

YUV4MPEGHeader::YUV4MPEGHeader()
  : width( 0 ), height( 0 ), fps_numerator( 0 ), fps_denominator( 0 ),
    pixel_aspect_ratio_numerator( 0 ), pixel_aspect_ratio_denominator( 0 ),
    interlacing_mode( PROGRESSIVE ), color_space( C420 )
{}

YUV4MPEGHeader::YUV4MPEGHeader( const BaseRaster &rh )
  : width( rh.display_width() )
  , height( rh.display_height() )
  , fps_numerator( 24 ), fps_denominator( 1 )
  , pixel_aspect_ratio_numerator( 1 ), pixel_aspect_ratio_denominator( 1 )
  , interlacing_mode( PROGRESSIVE ), color_space( C420 )
{}

size_t YUV4MPEGHeader::frame_length()
{
  switch ( color_space ) {
    case C420:
    case C420jpeg:
    case C420paldv:
      return ( width * height * 3 ) / 2;
    default: throw LogicError();
  }
}

size_t YUV4MPEGHeader::y_plane_length()
{
  switch ( color_space ) {
    case C420:
    case C420jpeg:
    case C420paldv:
      return ( width * height );
    default: throw LogicError();
  }
}

size_t YUV4MPEGHeader::uv_plane_length()
{
  switch ( color_space ) {
    case C420:
    case C420jpeg:
    case C420paldv:
      return ( width * height / 4 );
    default: throw LogicError();
  }
}

string YUV4MPEGHeader::to_string()
{
  stringstream ss;
  ss << "YUV4MPEG2 "
     << 'W' << width
     << ' '
     << 'H' << height
     << ' '
     << 'F' << fps_numerator << ':' << fps_denominator
     << ' '
     << 'I';

  switch ( interlacing_mode ) {
    case PROGRESSIVE:
      ss << 'p'; break;
    case TOP_FIELD_FIRST:
      ss << 't'; break;
    case BOTTOM_FIELD_FIRST:
      ss << 'b'; break;
    case MIXED_MODES:
      ss << 'm'; break;
  }

  ss << ' '
     << 'A' << pixel_aspect_ratio_numerator << ':' << pixel_aspect_ratio_denominator
     << ' ';

  switch ( color_space ) {
    case C420jpeg:
      ss << "C420jpeg XYSCSS=420JPEG"; break;
    case C420paldv:
      ss << "C420paldv XYSCSS=420PALDV"; break;
    case C420:
      ss << "C420 XYSCSS=420"; break;
    case C422:
      ss << "C422 XYSCSS=422"; break;
    case C444:
      ss << "C444 XYSCSS=444"; break;
  }

  ss << '\n';

  return ss.str();
}

pair< size_t, size_t > YUV4MPEGReader::parse_fraction( const string & fraction_str )
{
  size_t colon_pos = fraction_str.find( ":" );

  if ( colon_pos == string::npos ) {
    throw runtime_error( "invalid fraction" );
  }

  size_t numerator = 0;
  size_t denominator = 0;

  numerator = stoi( fraction_str.substr( 0, colon_pos ) );
  denominator = stoi( fraction_str.substr( colon_pos + 1 ) );

  return make_pair( numerator, denominator );
}

YUV4MPEGReader::YUV4MPEGReader( const string & filename )
  : YUV4MPEGReader( SystemCall( filename,
                    open( filename.c_str(),
                    O_RDWR | O_CREAT,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH ) ) )
{}

YUV4MPEGReader::YUV4MPEGReader( FileDescriptor && fd )
  : header_(),
    fd_( move( fd ) )
{
  string header_str = fd_.getline();
  istringstream ssin( header_str );

  string token;
  ssin >> token;

  if ( token != "YUV4MPEG2" ) {
    throw runtime_error( "invalid yuv4mpeg2 magic code" );
  }

  while ( ssin >> token ) {
    if ( token.length() == 0 ) {
      break;
    }

    switch ( token[0] ) {
    case 'W': // width
      header_.width = stoi( token.substr( 1 ) );
      break;

    case 'H': // height
      header_.height = stoi( token.substr( 1 ) );
      break;

    case 'F': // framerate
    {
      pair< size_t, size_t > fps = YUV4MPEGReader::parse_fraction( token.substr( 1 ) );
      header_.fps_numerator = fps.first;
      header_.fps_denominator = fps.second;
      break;
    }

    case 'I':
      if ( token.length() < 2 ) {
        throw runtime_error( "invalid interlacing mode" );
      }

      switch ( token[ 1 ] ) {
      case 'p': header_.interlacing_mode = YUV4MPEGHeader::InterlacingMode::PROGRESSIVE; break;
      case 't': header_.interlacing_mode = YUV4MPEGHeader::InterlacingMode::TOP_FIELD_FIRST; break;
      case 'b': header_.interlacing_mode = YUV4MPEGHeader::InterlacingMode::BOTTOM_FIELD_FIRST; break;
      case 'm': header_.interlacing_mode = YUV4MPEGHeader::InterlacingMode::MIXED_MODES; break;
      default: throw runtime_error( "invalid interlacing mode" );
      }
      break;

    case 'A': // pixel aspect ratio
    {
      pair< size_t, size_t > aspect_ratio = YUV4MPEGReader::parse_fraction( token.substr( 1 ) );
      header_.pixel_aspect_ratio_numerator = aspect_ratio.first;
      header_.pixel_aspect_ratio_denominator = aspect_ratio.second;
      break;
    }

    case 'C': // color space
        if ( token.substr( 0, 4 ) != "C420" ) {
          throw runtime_error( "only yuv420 color space is supported" );
        }
        header_.color_space = YUV4MPEGHeader::ColorSpace::C420;
        break;

    case 'X': // comment
      break;

    default:
      throw runtime_error( "invalid yuv4mpeg2 input format" );
    }
  }

  if ( header_.width == 0 or header_.height == 0 ) {
    throw runtime_error( "width or height missing" );
  }
}

void edge_extend_component( TwoD<uint8_t> & component,
                            const unsigned int display_width,
                            const unsigned int display_height )
{
  if ( component.width() == 0
       or component.height() == 0 ) {
    throw Invalid( "image is zero size" );
  }

  /* edge-extend to the right */
  for ( unsigned int row = 0; row < display_height; row++ ) {
    const uint8_t repeated_sample = component.at( display_width - 1, row );
    for ( unsigned int column = display_width; column < component.width(); column++ ) {
      component.at( column, row ) = repeated_sample;
    }
  }

  /* edge-extend to the bottom */
  for ( unsigned int row = display_height; row < component.height(); row++ ) {
    for ( unsigned int column = 0; column < display_width; column++ ) {
      const uint8_t repeated_sample = component.at( column, display_height - 1 );
      component.at( column, row ) = repeated_sample;
    }
  }

  /* edge-extend to the lower-right quadrant */
  const uint8_t repeated_sample = component.at( display_width - 1, display_height - 1 );

  for ( unsigned int row = display_height; row < component.height(); row++ ) {
    for ( unsigned int column = display_width; column < component.width(); column++ ) {
      component.at( column, row ) = repeated_sample;
    }
  }
}

void edge_extend( BaseRaster & raster )
{
  edge_extend_component( raster.Y(), raster.display_width(), raster.display_height() );
  edge_extend_component( raster.U(), raster.chroma_display_width(), raster.chroma_display_height() );
  edge_extend_component( raster.V(), raster.chroma_display_width(), raster.chroma_display_height() );
}

Optional<RasterHandle> YUV4MPEGReader::get_next_frame()
{
  MutableRasterHandle raster { header_.width, header_.height };

  string frame_header = fd_.getline();

  if ( fd_.eof() ) {
    return {};
  }

  if ( frame_header != "FRAME" ) {
    throw runtime_error( "invalid yuv4mpeg2 input format" );
  }

  string read_data;

  for ( size_t byte = 0; byte < y_plane_length(); byte += read_data.length() ) {
    read_data = fd_.read( y_plane_length() - byte );

    if ( fd_.eof() ) {
      throw Invalid( "partial YUV4MPEG frame" );
    }

    for ( size_t i = 0; i < read_data.length(); i++ ) {
      raster.get().Y().at( ( i + byte ) % header_.width,
                           ( i + byte ) / header_.width ) = read_data[ i ];
    }
  }

  for ( size_t byte = 0; byte < uv_plane_length(); byte += read_data.length() ) {
    read_data = fd_.read( uv_plane_length() - byte );

    if ( fd_.eof() ) {
      throw Invalid( "partial YUV4MPEG frame" );
    }

    for ( size_t i = 0; i < read_data.length(); i++ ) {
      raster.get().U().at( ( i + byte ) % ( header_.width / 2 ),
                           ( i + byte ) / ( header_.width / 2 ) ) = read_data[ i ];
    }
  }

  for ( size_t byte = 0; byte < uv_plane_length(); byte += read_data.length() ) {
    read_data = fd_.read( uv_plane_length() - byte );

    if ( fd_.eof() ) {
      throw Invalid( "partial YUV4MPEG frame" );
    }

    for ( size_t i = 0; i < read_data.length(); i++ ) {
      raster.get().V().at( ( i + byte ) % ( header_.width / 2 ),
                           ( i + byte ) / ( header_.width / 2 ) ) = read_data[ i ];
    }
  }

  /* edge-extend the raster */
  edge_extend( raster.get() );

  return { move( raster ) };
}

void YUV4MPEGFrameWriter::write( const BaseRaster &rh, FileDescriptor &fd )
{
  fd.write("FRAME\n");

  // display_rectangle_as_planar returns Y, U, V chunks in row-major order
  for ( const auto & chunk : rh.display_rectangle_as_planar() ) {
    fd.write( chunk );
  }
}
