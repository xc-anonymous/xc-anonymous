/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "exception.hh"
#include "display.hh"

using namespace std;

const string VideoDisplay::shader_source_scale_from_pixel_coordinates
= R"( #version 130

      uniform uvec2 window_size;

      in vec2 position;
      in vec2 chroma_texcoord;
      out vec2 raw_position;
      out vec2 uv_texcoord;

      void main()
      {
        gl_Position = vec4( 2 * position.x / window_size.x - 1.0,
                            1.0 - 2 * position.y / window_size.y, 0.0, 1.0 );
        raw_position = vec2( position.x, position.y );
        uv_texcoord = vec2( chroma_texcoord.x, chroma_texcoord.y );
      }
    )";

/* octave> 255 * inv([219*[.7152 .0722 .2126]'
                      224*[-0.38542789394266 .5 -0.11457210605734]'
                      224*[-0.454152908305817 -0.0458470916941834 .5]']') */

const string VideoDisplay::shader_source_ycbcr
= R"( #version 130
      #extension GL_ARB_texture_rectangle : enable

      precision mediump float;

      uniform sampler2DRect yTex;
      uniform sampler2DRect uTex;
      uniform sampler2DRect vTex;

      in vec2 uv_texcoord;
      in vec2 raw_position;
      out vec4 outColor;

      void main()
      {
        float fY = texture(yTex, raw_position).x;
        float fCb = texture(uTex, uv_texcoord).x;
        float fCr = texture(vTex, uv_texcoord).x;

        outColor = vec4(
          max(0, min(1.0, 1.16438439417611 * (fY - 0.06274509803921568627) + 1.79274107142857 * (fCr - 0.50196078431372549019))),
          max(0, min(1.0, 1.16438356164384 * (fY - 0.06274509803921568627) - 0.21324861427373 * (fCb - 0.50196078431372549019)
          - 0.532909328559444 * (fCr - 0.50196078431372549019))),
          max(0, min(1.0, 1.16438439417611 * (fY - 0.06274509803921568627) + 2.11240178571429 * (fCb - 0.50196078431372549019))),
          1.0
        );
      }
    )";

VideoDisplay::CurrentContextWindow::CurrentContextWindow( const unsigned int width,
  const unsigned int height,
  const string & title)
  : window_( width, height, title )
{
  window_.make_context_current( true );
}

VideoDisplay::VideoDisplay( const BaseRaster & raster )
  : display_width_( raster.display_width() ),
    display_height_( raster.display_height() ),
    width_( raster.width() ),
    height_( raster.height() ),
    current_context_window_( display_width_, display_height_, "VP8 Player" ),
    Y_ ( width_, height_),
    U_ ( width_ / 2, height_ / 2 ),
    V_ ( width_ / 2, height_ / 2 )
{
  texture_shader_program_.attach( scale_from_pixel_coordinates_ );
  texture_shader_program_.attach( ycbcr_shader_ );
  texture_shader_program_.link();
  glCheck( "after linking texture shader program" );

  texture_shader_array_object_.bind();
  ArrayBuffer::bind( screen_corners_ );
  glVertexAttribPointer( texture_shader_program_.attribute_location( "position" ),
    2, GL_FLOAT, GL_FALSE, sizeof( VertexObject ), 0 );
  glEnableVertexAttribArray( texture_shader_program_.attribute_location( "position" ) );

  glVertexAttribPointer( texture_shader_program_.attribute_location( "chroma_texcoord" ),
    2, GL_FLOAT, GL_FALSE, sizeof( VertexObject ), (const void *)( 2 * sizeof( float ) ) );
  glEnableVertexAttribArray( texture_shader_program_.attribute_location( "chroma_texcoord" ) );

  glfwSwapInterval(1);

  Y_.bind( GL_TEXTURE0 );
  U_.bind( GL_TEXTURE1 );
  V_.bind( GL_TEXTURE2 );

  const pair<unsigned int, unsigned int> window_size = window().size();
  resize( window_size );

  glCheck( "" );
}

void VideoDisplay::resize( const pair<unsigned int, unsigned int> & target_size )
{
  glViewport( 0, 0, target_size.first, target_size.second );

  texture_shader_program_.use();
  glUniform2ui( texture_shader_program_.uniform_location( "window_size" ),
                target_size.first, target_size.second );

  glUniform1i( texture_shader_program_.uniform_location( "yTex" ), 0);
  glUniform1i( texture_shader_program_.uniform_location( "uTex" ), 1);
  glUniform1i( texture_shader_program_.uniform_location( "vTex" ), 2);

  const float xoffset = 0.25;

  float target_size_x = target_size.first;
  float target_size_y = target_size.second;

  vector<VertexObject> corners = {
    { 0, 0, xoffset, 0},
    { 0, target_size_y, xoffset, target_size_y / 2 },
    { target_size_x, target_size_y, target_size_x / 2 + xoffset, target_size_y / 2 },
    { target_size_x, 0 , target_size_x / 2 + xoffset, 0 }
  };

  texture_shader_array_object_.bind();
  ArrayBuffer::bind( screen_corners_ );
  ArrayBuffer::load( corners, GL_STATIC_DRAW );

  glCheck( "after resizing ");
}

void VideoDisplay::draw( const BaseRaster & raster )
{
  if ( width_ != raster.width() or height_ != raster.height() ) {
    throw Invalid( "inconsistent raster dimensions." );
  }

  Y_.load( raster.Y() );
  U_.load( raster.U() );
  V_.load( raster.V() );

  repaint();
}

void VideoDisplay::repaint( void )
{
  pair<unsigned int, unsigned int> window_size = current_context_window_.window_.window_size();

  if ( window_size.first != display_width_ or window_size.second != display_height_ ) {
    display_width_ = window_size.first;
    display_height_ = window_size.second;
    resize( make_pair( display_width_, display_height_ ) );
  }

  ArrayBuffer::bind( screen_corners_ );
  texture_shader_array_object_.bind();
  texture_shader_program_.use();
  glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

  current_context_window_.window_.swap_buffers();
}
