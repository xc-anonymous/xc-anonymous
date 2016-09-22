/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include <iostream>
#include <vector>

#include "ivf.hh"
#include "uncompressed_chunk.hh"

using namespace std;

int main( int argc, char *argv[] )
{
  try {
    if ( argc != 2 ) {
      cerr << "Usage: " << argv[ 0 ] << " FILENAME" << endl;
      return EXIT_FAILURE;
    }

    IVF file( argv[ 1 ] );

    if ( file.fourcc() != "VP80" ) {
      throw Unsupported( "not a VP8 file" );
    }

    vector< uint32_t > key_frames;

    /* count key frames */
    unsigned int num_key_frames = 0;
    for ( uint32_t i = 0; i < file.frame_count(); i++ ) {
      UncompressedChunk chunk( file.frame( i ), file.width(), file.height() );
      if ( chunk.key_frame() ) {
        num_key_frames++;
        key_frames.emplace_back( i );
      }
    }

    cerr << "File contains " << num_key_frames << " key frames." << endl;

    /* write IVF file */
    cout << "DKIF";
    cout << uint8_t(0) << uint8_t(0); /* version */
    cout << uint8_t(32) << uint8_t(0); /* header length */
    cout << "VP80"; /* fourcc */
    cout << uint8_t(file.width() & 0xff) << uint8_t((file.width() >> 8) & 0xff); /* width */
    cout << uint8_t(file.height() & 0xff) << uint8_t((file.height() >> 8) & 0xff); /* height */
    cout << uint8_t(1) << uint8_t(0) << uint8_t(0) << uint8_t(0); /* bogus frame rate */
    cout << uint8_t(1) << uint8_t(0) << uint8_t(0) << uint8_t(0); /* bogus time scale */

    const uint32_t le_num_frames = htole32( num_key_frames );
    cout << string( reinterpret_cast<const char *>( &le_num_frames ), sizeof( le_num_frames ) ); /* num frames */

    cout << uint8_t(0) << uint8_t(0) << uint8_t(0) << uint8_t(0); /* fill out header */

    for ( const auto & i : key_frames ) {
      const auto & frame = file.frame( i );
      cerr << "size of frame " << i << ": " << frame.size() << endl;
      const uint32_t le_size = htole32( frame.size() );
      cout << string( reinterpret_cast<const char *>( &le_size ), sizeof( le_size ) ); /* size */

      cout << uint8_t(0) << uint8_t(0) << uint8_t(0) << uint8_t(0); /* fill out header */
      cout << uint8_t(0) << uint8_t(0) << uint8_t(0) << uint8_t(0); /* fill out header */

      cout << string( reinterpret_cast<const char *>( frame.buffer() ), frame.size() );
    }

  } catch ( const exception & e ) {
    print_exception( argv[ 0 ], e );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
