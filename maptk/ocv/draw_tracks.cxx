/*ckwg +5
 * Copyright 2014 by Kitware, Inc. All Rights Reserved. Please refer to
 * KITWARE_LICENSE.TXT for licensing information, or contact General Counsel,
 * Kitware, Inc., 28 Corporate Drive, Clifton Park, NY 12065.
 */

/**
 * \file
 * \brief Implementation of OCV draw tracks algorithm
 */

#include <maptk/ocv/draw_tracks.h>
#include <maptk/ocv/ocv_algo_tools.h>
#include <maptk/ocv/image_container.h>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <set>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace maptk
{

namespace ocv
{


/// Private implementation class
class draw_tracks::priv
{
public:

  /// Constructor
  priv()
  : draw_track_ids(true),
    draw_untracked_features(true),
    draw_match_lines(false),
    draw_shift_lines(true),
    pattern("feature_tracks_%1%.png")
  {
  }

  /// Copy Constructor
  priv(const priv& other)
  {
    *this = other;
  }

  /// Destructor
  ~priv()
  {
  }

  /// Parameters
  bool draw_track_ids;
  bool draw_untracked_features;
  bool draw_match_lines;
  bool draw_shift_lines;
  std::vector<unsigned> past_frames_to_show;
  boost::format pattern;
};


/// Constructor
draw_tracks
::draw_tracks()
: d_(new priv)
{
}


/// Copy constructor
draw_tracks
::draw_tracks(const draw_tracks& other)
: d_(new priv(*other.d_))
{
}


/// Destructor
draw_tracks
::~draw_tracks()
{
}


/// Get this algorithm's \link maptk::config_block configuration block \endlink
config_block_sptr
draw_tracks
::get_configuration() const
{
  config_block_sptr config = maptk::algo::draw_tracks::get_configuration();

  config->set_value( "draw_track_ids", d_->draw_track_ids,
                     "Draw track ids next to each feature point." );
  config->set_value( "draw_untracked_features", d_->draw_untracked_features,
                     "Draw untracked feature points in red." );
  config->set_value( "draw_match_lines", d_->draw_match_lines,
                     "Draw lines between tracked features on the current frame "
                     "to any past frames." );
  config->set_value( "draw_shift_lines", d_->draw_shift_lines,
                     "Draw lines showing the movement of the feature in the image "
                     "plane from the last frame to the current one." );
  config->set_value( "past_frames_to_show", "",
                     "A comma seperated list of past frames to show. For example: "
                     "a value of \"2, 1\" will cause the GUI to generate a window "
                     "3 frames wide, with the first frame being 2 frames behind the "
                     "current frame, the second 1 frame behind, and the third being "
                     "the current frame." );
  config->set_value( "pattern", "feature_tracks_%1%.png",
                     "The output pattern for drawn images." );

  return config;
}


/// Set this algorithm's properties via a config block
void
draw_tracks
::set_configuration(config_block_sptr in_config)
{
  config_block_sptr config = this->get_configuration();
  config->merge_config( in_config );

  std::string past_frames_str = config->get_value<std::string>( "past_frames_to_show" );

  std::stringstream ss( past_frames_str );

  unsigned next_int;
  while( ss >> next_int )
  {
    d_->past_frames_to_show.push_back( next_int );

    if( ss.peek() == ',' )
    {
      ss.ignore();
    }
  }

  d_->draw_track_ids = config->get_value<bool>( "draw_track_ids" );
  d_->draw_untracked_features = config->get_value<bool>( "draw_untracked_features" );
  d_->draw_match_lines = config->get_value<bool>( "draw_match_lines" );
  d_->draw_shift_lines = config->get_value<bool>( "draw_shift_lines" );
  d_->pattern = boost::format( config->get_value<std::string>( "pattern" ) );
}


/// Check that the algorithm's currently configuration is valid
bool
draw_tracks
::check_configuration(config_block_sptr config) const
{
  return true;
}


/// Output images with tracked features drawn on them
void
draw_tracks
::draw(track_set_sptr track_set,
       image_container_sptr_list image_data) const
{
  typedef std::vector< std::pair< cv::Point, cv::Point > > line_vec_t;

  // Validate inputs
  if( image_data.size() < track_set->last_frame() )
  {
    std::cerr << "Error: not enough imagery to display all tracks" << std::endl;
  }

  // The total number of past frames we are showing
  const unsigned past_frames = d_->past_frames_to_show.size();

  // Generate output images
  frame_id_t fid = 0;

  // The few last images with features drawn on them
  std::vector< cv::Mat > prior_images( past_frames );

  // Colors to use
  const cv::Scalar blue( 255, 0, 0 );
  const cv::Scalar red( 0, 0, 255 );
  const cv::Scalar green( 0, 255, 0 );
  const cv::Scalar purple( 240, 32, 160 );

  // Iterate over all images
  BOOST_FOREACH( image_container_sptr ctr_sptr, image_data )
  {
    // Paint active tracks on the input image
    cv::Mat img = ocv::image_container::maptk_to_ocv( ctr_sptr->get_image() );

    // Convert to 3 channel image if not one already
    if( img.channels() == 1 )
    {
      cv::cvtColor( img, img, CV_GRAY2BGR );
    }

    // List of lines to draw on final image
    std::vector< line_vec_t > lines( past_frames );

    // Adjustment added to bring a point to a seperate window
    const cv::Point pt_adj( img.cols, 0 );

    // Draw points on input image
    BOOST_FOREACH( track_sptr trk, track_set->active_tracks( fid )->tracks() )
    {
      track::track_state ts = *( trk->find( fid ) );

      if( !ts.feat )
      {
        continue;
      }

      // Handle drawing the feature point on the image
      cv::Scalar color = blue;
      cv::Point loc( ts.feat->loc()[0], ts.feat->loc()[1] );
      cv::Point txt_offset( 2, -2 );
      std::string tid_str = boost::lexical_cast<std::string>( trk->id() );

      if( trk->size() == 1 )
      {
        color = red;
      }
      else if( trk->first_frame() == fid )
      {
        color = green;
      }
      else if( trk->last_frame() == fid )
      {
        color = purple;
      }

      if( d_->draw_untracked_features || trk->size() > 1 )
      {
        cv::circle( img, loc, 1, color, 3 );
      }

      if( d_->draw_track_ids && trk->size() > 1 )
      {
        cv::putText( img, tid_str, loc + txt_offset, cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, color );
      }

      // Generate and draw shift lines on the video
      if( d_->draw_shift_lines && trk->size() > 1 && fid > 0 )
      {
        track::history_const_itr itr = trk->find( fid-1 );

        if( itr != trk->end() && itr->feat )
        {
          cv::Point prior_loc( itr->feat->loc()[0], itr->feat->loc()[1] );

          if( d_->draw_shift_lines )
          {
            cv::line( img, prior_loc, loc, blue );
          }
        }
      }

      // Generate and store match lines for later use
      if( d_->draw_match_lines )
      {
        for( unsigned i = 0;
        if( fid > 1 )
        {
          track::history_const_itr itr2 = trk->find( fid-2 );

          if( itr2 != trk->end() && itr2->feat && itr == trk->end() )
          {
            cv::Point prior_loc( itr2->feat->loc()[0], itr2->feat->loc()[1] );

            if( d_->draw_match_lines )
            {
              cur_to_thr_lines.push_back( std::make_pair( prior_loc, loc ) );
            }
          }
        }
      }
    }

    // Output image
    std::string ofn = boost::str( d_->pattern % fid );

    if( d_->display_type == priv::TRI_WINDOW )
    {
      cv::Mat unioned_image( img.rows, 3*img.cols, img.type(), cv::Scalar(0) );

      cv::Mat left( unioned_image, cv::Rect( 0, 0, img.cols, img.rows ) );
      cv::Mat middle( unioned_image, cv::Rect( img.cols, 0, img.cols, img.rows ) );
      cv::Mat right( unioned_image, cv::Rect( 2*img.cols, 0, img.cols, img.rows ) );

      if( fid > 1 )
      {
        prior_images[1].copyTo( left );
      }
      if( fid > 0 )
      {
        prior_images[0].copyTo( middle );
      }
      img.copyTo( right );

      for( unsigned i = 0; i < cur_to_sec_lines.size(); i++ )
      {
        cv::line( unioned_image, cur_to_sec_lines[i].first + pt_adj,
                  cur_to_sec_lines[i].second + 2*pt_adj, blue );
      }
      for( unsigned i = 0; i < cur_to_thr_lines.size(); i++ )
      {
        cv::line( unioned_image, cur_to_thr_lines[i].first,
                  cur_to_thr_lines[i].second + 2*pt_adj, blue );
      }

      cv::imwrite( ofn.c_str(), unioned_image );
    }
    else if( d_->display_type == priv::DUAL_WINDOW )
    {
      cv::Mat unioned_image( img.rows, 2*img.cols, img.type(), cv::Scalar(0) );

      cv::Mat left( unioned_image, cv::Rect( 0, 0, img.cols, img.rows ) );
      cv::Mat right( unioned_image, cv::Rect( img.cols, 0, img.cols, img.rows ) );

      if( fid > 0 )
      {
        prior_images[0].copyTo( left );
      }
      img.copyTo( right );

      for( unsigned i = 0; i < cur_to_sec_lines.size(); i++ )
      {
        cv::line( unioned_image, cur_to_sec_lines[i].first,
                  cur_to_sec_lines[i].second + pt_adj, blue );
      }

      cv::imwrite( ofn.c_str(), unioned_image );
    }
    else
    {
      cv::imwrite( ofn.c_str(), img );
    }

    // Store last variables
    prior_images[1] = prior_images[0];
    prior_images[0] = img;
    prior_lines = cur_to_sec_lines;
    fid++;
  }
}


} // end namespace ocv

} // end namespace maptk
