#include <fstream>
#include "ScubaLayer2DMRI.h"
#include "ViewState.h"
#include "talairachex.h"
#include "TclDlogManager.h"
#include "Utilities.h"
#include "Array2.h"

using namespace std;

int const ScubaLayer2DMRI::cGrayscaleLUTEntries = 256;
int const ScubaLayer2DMRI::kMaxPixelComponentValue = 255;

ScubaLayer2DMRI::ScubaLayer2DMRI () {

  SetOutputStreamToCerr();

  mVolume = NULL;
  mSampleMethod = nearest;
  mColorMapMethod = grayscale;
  mBrightness = 0.25;
  mContrast = 12.0;
  mCurrentLine = NULL;
  mCurrentSnakeLine = NULL;
  mROIOpacity = 0.7;

  // Try setting our initial color LUT to the default LUT with
  // id 0. If it's not there, create it.
  try { 
    mColorLUT = &(ScubaColorLUT::FindByID( 0 ));
  }
  catch(...) {

    ScubaColorLUT* lut = new ScubaColorLUT();
    lut->SetLabel( "Default" );
    
    try {
      mColorLUT = &(ScubaColorLUT::FindByID( 0 ));
    }
    catch(...) {
      DebugOutput( << "Couldn't make default lut!" );
    }
  }

  TclCommandManager& commandMgr = TclCommandManager::GetManager();
  commandMgr.AddCommand( *this, "Set2DMRILayerVolumeCollection", 2, 
			 "layerID collectionID",
			 "Sets the volume collection for this layer." );
  commandMgr.AddCommand( *this, "Set2DMRILayerColorMapMethod", 2, 
			 "layerID method",
			 "Sets the color map method for this layer." );
  commandMgr.AddCommand( *this, "Get2DMRILayerColorMapMethod", 1, "layerID",
			 "Returns the color map method for this layer." );
  commandMgr.AddCommand( *this, "Set2DMRILayerSampleMethod", 2, 
			 "layerID method",
			 "Sets the sample method for this layer." );
  commandMgr.AddCommand( *this, "Get2DMRILayerSampleMethod", 1, "layerID",
			 "Returns the sample method for this layer." );
  commandMgr.AddCommand( *this, "Set2DMRILayerBrightness", 2, 
			 "layerID brightness",
			 "Sets the brightness for this layer." );
  commandMgr.AddCommand( *this, "Get2DMRILayerBrightness", 1, "layerID",
			 "Returns the brightness for this layer." );
  commandMgr.AddCommand( *this, "Set2DMRILayerContrast", 2, 
			 "layerID contrast",
			 "Sets the contrast for this layer." );
  commandMgr.AddCommand( *this, "Get2DMRILayerContrast", 1, "layerID",
			 "Returns the contrast for this layer." );
  commandMgr.AddCommand( *this, "Set2DMRILayerColorLUT", 2, "layerID lutID",
			 "Sets the LUT  for this layer." );
  commandMgr.AddCommand( *this, "Get2DMRILayerColorLUT", 1, "layerID",
			 "Returns the LUT id for this layer." );
  commandMgr.AddCommand( *this, "Set2DMRILayerDrawZeroClear", 2, 
			 "layerID drawClear", "Sets property for drawing"
			 "values of zero clear." );
  commandMgr.AddCommand( *this, "Get2DMRILayerDrawZeroClear", 1, "layerID",
			 "Returns the value of the property for drawing"
			 "values of zero clear." );
  commandMgr.AddCommand( *this, "Set2DMRILayerMinVisibleValue", 2, 
			 "layerID value", "Sets the minimum value to be drawn."
			 "values of zero clear." );
  commandMgr.AddCommand( *this, "Get2DMRILayerMinVisibleValue", 1, "layerID",
			 "Returns the minimum value to be drawn." );
  commandMgr.AddCommand( *this, "Set2DMRILayerMaxVisibleValue", 2, 
			 "layerID value", "Sets the maximum value to be drawn."
			 "values of zero clear." );
  commandMgr.AddCommand( *this, "Get2DMRILayerMaxVisibleValue", 1, "layerID",
			 "Returns the maximum value to be drawn." );
  commandMgr.AddCommand( *this, "Get2DMRILayerMinValue", 1, "layerID",
			 "Returns the minimum value of the volume." );
  commandMgr.AddCommand( *this, "Get2DMRILayerMaxValue", 1, "layerID",
			 "Returns the maximum value of the volume." );
  commandMgr.AddCommand( *this, "Set2DMRILayerROIOpacity", 2,"layerID opacity",
			 "Sets the opacity of the ROI for a layer." );
  commandMgr.AddCommand( *this, "Get2DMRILayerROIOpacity", 1, "layerID",
			 "Returns the opacity of the ROI for a layer." );
  
}

ScubaLayer2DMRI::~ScubaLayer2DMRI () {

}

void
ScubaLayer2DMRI::SetVolumeCollection ( VolumeCollection& iVolume ) {

  mVolume = &iVolume;

  mVolume->GetMRI();
  mMinVisibleValue = mVolume->GetMRIMinValue();
  mMaxVisibleValue = mVolume->GetMRIMaxValue();

  BuildGrayscaleLUT();
}

void 
ScubaLayer2DMRI::DrawIntoBuffer ( GLubyte* iBuffer, int iWidth, int iHeight,
				  ViewState& iViewState,
				  ScubaWindowToRASTranslator& iTranslator ) {

  if( NULL == mVolume ) {
    DebugOutput( << "No volume to draw" );
    return;
  }

  float midValue;
  midValue = (mMaxVisibleValue - mMinVisibleValue) / 2.0 + mMinVisibleValue;

  GLubyte* dest = iBuffer;

  // Point to the beginning of the buffer. For each pixel in the
  // buffer...
  int window[2];
  for( window[1] = 0; window[1] < iHeight; window[1]++ ) {
    for( window[0] = 0; window[0] < iWidth; window[0]++ ) {

      // Use our translator to get an RAS point.
      float RAS[3];
      iTranslator.TranslateWindowToRAS( window, RAS );

      // Make sure this is within the bounds. If it is...
      int color[3];
      color[0] = color[1] = color[2] = 0;
      if( mVolume->IsRASInMRIBounds( RAS ) ) {


	float value = 0;
	switch( mSampleMethod ) {
	case nearest:
	  value = mVolume->GetMRINearestValueAtRAS( RAS ); 
 	  break;
	case trilinear:
	  value = mVolume->GetMRITrilinearValueAtRAS( RAS ); 
	  break;
	case sinc:
	  value = mVolume->GetMRISincValueAtRAS( RAS ); 
	  break;
	case magnitude:
	  value = mVolume->GetMRIMagnitudeValueAtRAS( RAS );
	  break;
	}
	
	if( (mColorMapMethod == heatScale || value >= mMinVisibleValue) &&
	    (mColorMapMethod == heatScale || value <= mMaxVisibleValue) &&
	    ((value != 0 && mbClearZero) || !mbClearZero) ) {
	  
	  switch( mColorMapMethod ) { 
	  case grayscale: {

	    int nLUT = (int) floor( (cGrayscaleLUTEntries-1) * 
				    ((value - mMinVisibleValue) /
				     (mMaxVisibleValue - mMinVisibleValue)) );
	    if( nLUT < 0 ) nLUT = 0;
	    if( nLUT > cGrayscaleLUTEntries ) nLUT = cGrayscaleLUTEntries-1;
	    color[0] = (int) mGrayscaleLUT[nLUT];
	    color[1] = color[2] = color[0];
	  }
	    break;
	  case heatScale:
	    
	    if( fabs(value) >= mMinVisibleValue &&
		fabs(value) <= mMaxVisibleValue ) {
	      
	      float tmp;
	      if ( fabs(value) > mMinVisibleValue &&
		   fabs(value) < midValue ) {
		tmp = fabs(value);
		tmp = (1.0/(midValue-mMinVisibleValue)) *
		  (tmp-mMinVisibleValue)*(tmp-mMinVisibleValue) + 
		  mMinVisibleValue;
		value = (value<0) ? -tmp : tmp;
	      }
	      
	      /* calc the color */
	      float red, green, blue;
	      if( value >= 0 ) {
		red = ((value<mMinVisibleValue) ? 0.0 : 
		       (value<midValue) ? 
		       (value-mMinVisibleValue)/(midValue-mMinVisibleValue) :
		       1.0);
		green = ((value<midValue) ? 0.0 :
			 (value<mMaxVisibleValue) ? 
			 (value-midValue)/(mMaxVisibleValue-midValue) : 1.0);
		blue = 0.0; 
	      } else {
		value = -value;
		red = 0.0;
		green = ((value<midValue) ? 0.0 :
			 (value<mMaxVisibleValue) ? 
			 (value-midValue)/(mMaxVisibleValue-midValue) : 1.0);
		blue = ((value<mMinVisibleValue) ? 0.0 :
			(value<midValue) ? 
			(value-mMinVisibleValue)/(midValue-mMinVisibleValue) : 
			1.0);
	      }
	      
	      if( red > 1.0 )   red = 1.0;
	      if( green > 1.0 ) green = 1.0;
	      if( blue > 1.0 )  blue = 1.0;
	      
	      color[0] = (int) (red * (float)kMaxPixelComponentValue);
	      color[1] = (int) (green * (float)kMaxPixelComponentValue);
	      color[2] = (int) (blue * (float)kMaxPixelComponentValue);
	    } else {
	      color[0] = (int) dest[0];
	      color[1] = (int) dest[1];
	      color[2] = (int) dest[2];
	    }
	    break;
	  case LUT:
	    if( NULL != mColorLUT ) {
	      mColorLUT->GetColorAtIndex( (int)value, color );
	    }
	    break;
	  }
	}	

#if 0
	if( mVolume->IsRASEdge( RAS ) ) {
	  color[0] = 255; color[1] = color[2] = 0;
	}
#endif

	// Write the RGB value to the buffer. Write a 255 in the
	// alpha byte.
	dest[0] = (GLubyte) (((float)dest[0] * (1.0 - mOpacity)) +
			     ((float)color[0] * mOpacity));
	dest[1] = (GLubyte) (((float)dest[1] * (1.0 - mOpacity)) +
			     ((float)color[1] * mOpacity));
	dest[2] = (GLubyte) (((float)dest[2] * (1.0 - mOpacity)) +
			     ((float)color[2] * mOpacity));
	dest[3] = (GLubyte)255;
      }

      // Advance our pixel buffer pointer.
      dest += 4;
      
    }
  }


  dest = iBuffer;
  for( window[1] = 0; window[1] < iHeight; window[1]++ ) {
    for( window[0] = 0; window[0] < iWidth; window[0]++ ) {

      // Use our translator to get an RAS point.
      float RAS[3];
      iTranslator.TranslateWindowToRAS( window, RAS );

      // Make sure this is within the bounds. If it is...
      if( mVolume->IsRASInMRIBounds( RAS ) ) {
	
	int selectColor[3];
	if( mVolume->IsRASSelected( RAS, selectColor ) ) {
	  
	  // Write the RGB value to the buffer. Write a 255 in the
	  // alpha byte.
	  dest[0] = (GLubyte) (((float)dest[0] * (1.0 - mROIOpacity)) +
			       ((float)selectColor[0] * mROIOpacity));
	  dest[1] = (GLubyte) (((float)dest[1] * (1.0 - mROIOpacity)) +
			       ((float)selectColor[1] * mROIOpacity));
	  dest[2] = (GLubyte) (((float)dest[2] * (1.0 - mROIOpacity)) +
			       ((float)selectColor[2] * mROIOpacity));
	}
      }
      
      // Advance our pixel buffer pointer.
      dest += 4;
      
    }
  }


  // Line range.
  float range = 0;
  switch( iViewState.mInPlane ) {
  case 0: range = mVolume->GetVoxelXSize() / 2.0; break;
  case 1: range = mVolume->GetVoxelYSize() / 2.0; break;
  case 2: range = mVolume->GetVoxelZSize() / 2.0; break;
  }

  // Drawing straight lines.
  if( mCurrentLine ) {
    DrawStraightLineIntoBuffer( iBuffer, iWidth, iHeight, iViewState,
				iTranslator, mCurrentLine );
  }
  std::list<Line*>::iterator tLine;
  for( tLine = mLines.begin(); tLine != mLines.end(); ++tLine ) {
    Line* line = *tLine;
    if( iViewState.IsRASVisibleInPlane( line->mBeginRAS, range ) &&
	iViewState.IsRASVisibleInPlane( line->mEndRAS, range ) ) {
      DrawStraightLineIntoBuffer( iBuffer, iWidth, iHeight, iViewState,
				  iTranslator, line );
    }
  }


  // Drawing snake lines.
  if( mCurrentSnakeLine ) {
    DrawSnakeLineIntoBuffer( iBuffer, iWidth, iHeight, iViewState,
			     iTranslator, mCurrentSnakeLine );
  }
  std::list<SnakeLine*>::iterator tSnakeLine;
  for( tSnakeLine = mSnakeLines.begin(); 
       tSnakeLine != mSnakeLines.end();
       ++tSnakeLine ) {
    SnakeLine* line = *tSnakeLine;
    if( iViewState.IsRASVisibleInPlane( line->mBeginRAS.xyz(), range ) &&
	iViewState.IsRASVisibleInPlane( line->mEndRAS.xyz(), range ) ) {
      DrawSnakeLineIntoBuffer( iBuffer, iWidth, iHeight, iViewState,
			       iTranslator, line );
    }
  }

}
  
void 
ScubaLayer2DMRI::GetInfoAtRAS ( float iRAS[3], 
				map<string,string>& iLabelValues ) {

  if( NULL == mVolume ) {
    return;
  }

  // Look up the value of the volume at this point.
  if ( mVolume->IsRASInMRIBounds( iRAS ) ) {
    
    float value;
    value = mVolume->GetMRINearestValueAtRAS( iRAS ); 

    // If this is a LUT volume, use the label from the lookup file,
    // otherwise just display the value.
    stringstream ssValue;
    if( mColorMapMethod == LUT && NULL != mColorLUT ) {
      ssValue << mColorLUT->GetLabelAtIndex((int)value);
    } else {
      ssValue << value;
    }

    iLabelValues[mVolume->GetLabel() + " value"] = ssValue.str();

    int index[3];
    mVolume->RASToMRIIndex( iRAS, index );

    stringstream ssIndex;
    ssIndex << index[0] << " " << index[1] << " " << index[2];
    iLabelValues[mVolume->GetLabel() + " index"] = ssIndex.str();
  }
}
  
TclCommandListener::TclCommandResult 
ScubaLayer2DMRI::DoListenToTclCommand ( char* isCommand, int iArgc, char** iasArgv ) {

  // Set2DMRILayerVolumeCollection <layerID> <collectionID>
  if( 0 == strcmp( isCommand, "Set2DMRILayerVolumeCollection" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      int collectionID = strtol(iasArgv[2], (char**)NULL, 10);
      if( ERANGE == errno ) {
	sResult = "bad collection ID";
	return error;
      }
	
      try { 
	DataCollection& data = DataCollection::FindByID( collectionID );
	if( data.GetID() != collectionID ) {
	  cerr << "IDs didn't match" << endl;
	}
	VolumeCollection& volume = (VolumeCollection&)data;
	// VolumeCollection& volume = dynamic_cast<VolumeCollection&>(data);
	
	SetVolumeCollection( volume );
      }
      catch( std::bad_cast& e ) {
	DebugOutput( << "Bad cast from DataCollection" );
	sResult = "bad collection ID, collection not a volume collection";
	return error;
      }
      catch(...) {
	sResult = "bad collection ID, collection not found";
	return error;
      }
    }
  }

  // Set2DMRILayerColorMapMethod <layerID> <method>
  if( 0 == strcmp( isCommand, "Set2DMRILayerColorMapMethod" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      ColorMapMethod method;
      if( 0 == strcmp( iasArgv[2], "grayscale" ) ) {
	method = grayscale;
      } else if( 0 == strcmp( iasArgv[2], "heatScale" ) ) {
	method = heatScale;
      } else if( 0 == strcmp( iasArgv[2], "lut" ) ) {
	method = LUT;
      } else {
	sResult = "bad method \"" + string(iasArgv[2]) +
	  "\", should be grayscale, heatScale or LUT";
	return error;
      }

      SetColorMapMethod( method );
    }
  }

  // Get2DMRILayerColorMapMethod <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerColorMapMethod" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      switch ( mColorMapMethod ) {
      case grayscale:
	sReturnValues = "grayscale";
	break;
      case heatScale:
	sReturnValues = "heatScale";
	break;
      case LUT:
	sReturnValues = "lut";
	break;
      }
      sReturnFormat = "s";
    }
  }

  // Set2DMRILayerSampleMethod <layerID> <sampleMethod>
  if( 0 == strcmp( isCommand, "Set2DMRILayerSampleMethod" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      SampleMethod sampleMethod;
      if( 0 == strcmp( iasArgv[2], "nearest" ) ) {
	sampleMethod = nearest;
      } else if( 0 == strcmp( iasArgv[2], "trilinear" ) ) {
	sampleMethod = trilinear;
      } else if( 0 == strcmp( iasArgv[2], "sinc" ) ) {
	sampleMethod = sinc;
      } else if( 0 == strcmp( iasArgv[2], "magnitude" ) ) {
	sampleMethod = magnitude;
      } else {
	sResult = "bad sampleMethod \"" + string(iasArgv[2]) +
	  "\", should be nearest, trilinear, or sinc";
	return error;
      }
      
      SetSampleMethod( sampleMethod );
    }
  }

  // Get2DMRILayerSampleMethod <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerSampleMethod" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      switch( mSampleMethod ) {
      case nearest:
	sReturnValues = "nearest";
	break;
      case trilinear:
	sReturnValues = "trilinear";
	break;
      case sinc:
	sReturnValues = "sinc";
	break;
      case magnitude:
	sReturnValues = "magnitude";
	break;
      }
      sReturnFormat = "s";
    }
  }

  // Set2DMRILayerBrightness <layerID> <brightness>
  if( 0 == strcmp( isCommand, "Set2DMRILayerBrightness" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      float brightness = strtof( iasArgv[2], (char**)NULL );
      if( ERANGE == errno ) {
	sResult = "bad brightness";
	return error;
      }

      SetBrightness( brightness );
      BuildGrayscaleLUT();
    }
  }

  // Get2DMRILayerBrightness <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerBrightness" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      stringstream ssReturnValues;
      ssReturnValues << mBrightness;
      sReturnValues = ssReturnValues.str();
      sReturnFormat = "f";
    }
  }

  // Set2DMRILayerContrast <layerID> <contrast>
  if( 0 == strcmp( isCommand, "Set2DMRILayerContrast" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      float contrast = strtof( iasArgv[2], (char**)NULL );
      if( ERANGE == errno ) {
	sResult = "bad contrast";
	return error;
      }

      SetContrast( contrast );
      BuildGrayscaleLUT();
    }
  }

  // Get2DMRILayerContrast <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerContrast" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      stringstream ssReturnValues;
      ssReturnValues << mContrast;
      sReturnValues = ssReturnValues.str();
      sReturnFormat = "f";
    }
  }

  // Set2DMRILayerColorLUT <layerID> <lutID>
  if( 0 == strcmp( isCommand, "Set2DMRILayerColorLUT" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      int lutID = strtol(iasArgv[2], (char**)NULL, 10);
      if( ERANGE == errno ) {
	sResult = "bad lut ID";
	return error;
      }
    
      SetColorLUT( lutID );
    }
  }

  // Get2DMRILayerColorLUT <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerColorLUT" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      stringstream ssReturnValues;
      if( NULL != mColorLUT ) {
	ssReturnValues << mColorLUT->GetID();
      } else {
	ssReturnValues << -1;
      }
      sReturnValues = ssReturnValues.str();
      sReturnFormat = "i";

    }
  }

  // Set2DMRIDrawZeroClear <layerID> <drawClear>
  if( 0 == strcmp( isCommand, "Set2DMRILayerDrawZeroClear" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      if( 0 == strcmp( iasArgv[2], "true" ) || 
	  0 == strcmp( iasArgv[2], "1" )) {
	mbClearZero = true;
      } else if( 0 == strcmp( iasArgv[2], "false" ) ||
		 0 == strcmp( iasArgv[2], "0" ) ) {
	mbClearZero = false;
      } else {
	sResult = "bad drawClear \"" + string(iasArgv[2]) +
	  "\", should be true, 1, false, or 0";
	return error;	
      }
    }
  }

  // Get2DMRIDrawZeroClear <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerDrawZeroClear" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      stringstream ssReturnValues;
      ssReturnValues << (int)mbClearZero;
      sReturnValues = ssReturnValues.str();
      sReturnFormat = "i";
    }
  }

  // Get2DMRILayerMinVisibleValue <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerMinVisibleValue" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      sReturnFormat = "f";
      stringstream ssReturnValues;
      ssReturnValues << GetMinVisibleValue();
      sReturnValues = ssReturnValues.str();
    }
  }

  // Set2DMRILayerMinVisibleValue <layerID> <value>
  if( 0 == strcmp( isCommand, "Set2DMRILayerMinVisibleValue" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      float value = strtof( iasArgv[2], (char**)NULL );
      if( ERANGE == errno ) {
	sResult = "bad value";
	return error;
      }
      
      SetMinVisibleValue( value );
      BuildGrayscaleLUT();
    }
  }

  // Get2DMRILayerMaxVisibleValue <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerMaxVisibleValue" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      sReturnFormat = "f";
      stringstream ssReturnValues;
      ssReturnValues << GetMaxVisibleValue();
      sReturnValues = ssReturnValues.str();
    }
  }

  // Set2DMRILayerMaxVisibleValue <layerID> <value>
  if( 0 == strcmp( isCommand, "Set2DMRILayerMaxVisibleValue" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      float value = strtof( iasArgv[2], (char**)NULL );
      if( ERANGE == errno ) {
	sResult = "bad value";
	return error;
      }
      
      SetMaxVisibleValue( value );
      BuildGrayscaleLUT();
    }
  }
  
  // Get2DMRILayerMinValue <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerMinValue" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      sReturnFormat = "f";
      stringstream ssReturnValues;
      ssReturnValues << mVolume->GetMRIMinValue();
      sReturnValues = ssReturnValues.str();
    }
  }

  // Get2DMRILayerMaxValue <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerMaxValue" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      sReturnFormat = "f";
      stringstream ssReturnValues;
      ssReturnValues << mVolume->GetMRIMaxValue();
      sReturnValues = ssReturnValues.str();
    }
  }

  // Get2DMRILayerROIOpacity <layerID>
  if( 0 == strcmp( isCommand, "Get2DMRILayerROIOpacity" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      sReturnFormat = "f";
      stringstream ssReturnValues;
      ssReturnValues << GetROIOpacity();
      sReturnValues = ssReturnValues.str();
    }
  }

  // Set2DMRILayerROIOpacity <layerID> <opacity>
  if( 0 == strcmp( isCommand, "Set2DMRILayerROIOpacity" ) ) {
    int layerID = strtol(iasArgv[1], (char**)NULL, 10);
    if( ERANGE == errno ) {
      sResult = "bad layer ID";
      return error;
    }
    
    if( mID == layerID ) {
      
      float opacity = strtof( iasArgv[2], (char**)NULL );
      if( ERANGE == errno ) {
	sResult = "bad opacity";
	return error;
      }
      
      SetROIOpacity( opacity );
    }
  }
  
  return Layer::DoListenToTclCommand( isCommand, iArgc, iasArgv );
}

void
ScubaLayer2DMRI::HandleTool ( float iRAS[3], ViewState& iViewState,
			      ScubaWindowToRASTranslator& iTranslator,
			      ScubaToolState& iTool, InputState& iInput ) {
  
  switch( iTool.GetMode() ) {
  case ScubaToolState::voxelEditing:
    
    switch( iInput.Button() ) {
    case 2:
      mVolume->SetMRIValueAtRAS( iRAS, 255 );
      RequestRedisplay();
      break;
    }
    break;

  case ScubaToolState::roiEditing:

    // If shift key is down, we're filling. Make a flood params object
    // and fill it out, then make a flood select object, specifying
    // select or unselect in the ctor. Then run the flood object with
    // the params.
    if( iInput.IsShiftKeyDown() ) {

      if( iInput.IsButtonDownEvent() ) {

	VolumeCollectionFlooder::Params params;
	params.mbStopAtEdges = iTool.GetFloodStopAtLines();
	params.mbStopAtROIs  = iTool.GetFloodStopAtROIs();
	params.mb3D          = iTool.GetFlood3D();
	params.mFuzziness    = iTool.GetFloodFuzziness();
	params.mMaxDistance  = iTool.GetFloodMaxDistance();
	if( !iTool.GetFlood3D() ) {
	  params.mbWorkPlaneX = (iViewState.mInPlane == 0);
	  params.mbWorkPlaneY = (iViewState.mInPlane == 1);
	  params.mbWorkPlaneZ = (iViewState.mInPlane == 2);
	}
	
	// Create and run the flood object.
	switch( iInput.Button() ) {
	case 2: {
	  ScubaLayer2DMRIFloodSelect select( true );
	  select.Flood( *mVolume, iRAS, params );
	}
	  break;
	case 3: {
	  ScubaLayer2DMRIFloodSelect select( false );
	  select.Flood( *mVolume, iRAS, params );
	}
	  break;
	}
	RequestRedisplay();
      }
      
    } else {

      // Otherwise we're just brushing. If this is a mouse down event,
      // open up an undo action, and if it's a mouse up event, close
      // it. Then if the mouse is down switch on the brush shape and
      // call a GetRASPointsIn{Shape} function to get the points we
      // need to brush.

      UndoManager& undoList = UndoManager::GetManager();
      if( iInput.IsButtonDownEvent() ) {
	if( iInput.Button() == 2 ) {
	  undoList.BeginAction( "Selection Brush" );
	} else if( iInput.Button() == 3 ) {
	  undoList.BeginAction( "Unselection Brush" );
	}
      }

      if( iInput.IsButtonUpEvent() ) {
	undoList.EndAction();
      }

      if( iInput.IsButtonDown() ) {

	bool bBrushX, bBrushY, bBrushZ;
	bBrushX = bBrushY = bBrushZ = true;
	if( !iTool.GetBrush3D() ) {
	  bBrushX = !(iViewState.mInPlane == 0);
	  bBrushY = !(iViewState.mInPlane == 1);
	  bBrushZ = !(iViewState.mInPlane == 2);
	}
	list<Point3<float> > points;
	ScubaToolState::Shape shape = iTool.GetBrushShape();
	switch( shape ) {
	case ScubaToolState::square:
	  mVolume->GetRASPointsInCube( iRAS, iTool.GetBrushRadius(), 
				       bBrushX, bBrushY, bBrushZ, points );
	  break;
	case ScubaToolState::circle:
	  mVolume->GetRASPointsInSphere( iRAS, iTool.GetBrushRadius(), 
					 bBrushX, bBrushY, bBrushZ, points );
	  break;
	}
	
	switch( iInput.Button() ) {
	case 2: {
	  list<Point3<float> >::iterator tPoints;
	  for( tPoints = points.begin(); tPoints != points.end(); ++tPoints ) {
	    Point3<float> point = *tPoints;
	    mVolume->SelectRAS( point.xyz() );
	    UndoSelectionAction* action = 
	      new UndoSelectionAction( mVolume, true, point.xyz() );
	    undoList.AddAction( action );
	  }
	  undoList.EndAction();
	}
	  break;
	case 3:{
	  list<Point3<float> >::iterator tPoints;
	  for( tPoints = points.begin(); tPoints != points.end(); ++tPoints ) {
	    Point3<float> point = *tPoints;
	    mVolume->UnselectRAS( point.xyz() );
	    UndoSelectionAction* action = 
	      new UndoSelectionAction( mVolume, false, point.xyz() );
	    undoList.AddAction( action );
	  }
	}
	  
	  break;
	}

	RequestRedisplay();
      }
    }
    break;

  case ScubaToolState::straightLine:

    switch( iInput.Button() ) {
    case 1: 

      // If our button is down, if it's a new click, start a line. If
      // it's dragging, just streatch the line. If the button is not
      // down, it's a mouse up, and we'll end the line.
      if( iInput.IsButtonDownEvent() ) {
	StartLine( iRAS );
      } else if( iInput.IsButtonDragEvent() ) {
	StretchCurrentLine( iRAS );
      } else if( iInput.IsButtonUpEvent() ) {
	EndLine( iRAS, iTranslator );
      }

      RequestRedisplay();
      break;
    }
    break;

  case ScubaToolState::edgeLine:

    switch( iInput.Button() ) {
    case 1: 

      // If our button is down, if it's a new click, start a line. If
      // it's dragging, just streatch the line. If the button is not
      // down, it's a mouse up, and we'll end the line.
      if( iInput.IsButtonDownEvent() ) {
	StartEdgeLine( iRAS );
      } else if( iInput.IsButtonDragEvent() ) {
	StretchCurrentEdgeLine( iRAS, iViewState, iTranslator );
      } else if( iInput.IsButtonUpEvent() ) {
	EndEdgeLine( iRAS, iTranslator );
      }

      RequestRedisplay();
      break;
    }
    break;
    default:
      break;
    }
}

void
ScubaLayer2DMRI::SetColorLUT ( int iLUTID ) {

  try {
    mColorLUT = &(ScubaColorLUT::FindByID( iLUTID ));
  }
  catch(...) {
    DebugOutput( << "Couldn't find color LUT " << iLUTID );
  }
  
}


void
ScubaLayer2DMRI::BuildGrayscaleLUT () {

  for( float nEntry = 0; nEntry < cGrayscaleLUTEntries; nEntry+=1 ) {

    // Get the value using the actual min/max to get highest
    // granularity within the 0 - cGrayscaleLUTEntries range.
    float value = ((nEntry * (mMaxVisibleValue-mMinVisibleValue)) / 
		   cGrayscaleLUTEntries) + mMinVisibleValue;

    // Use sigmoid to apply brightness/contrast. Gets 0-1 value.
    float bcdValue = (1.0 / (1.0 + exp( (((value-mMinVisibleValue)/(mMaxVisibleValue-mMinVisibleValue))-mBrightness) * -mContrast)));

    // Normalize back to pixel component value.
    float normValue = bcdValue * (float)kMaxPixelComponentValue;

    // Assign in table.
    mGrayscaleLUT[(int)nEntry] = normValue;
  }
}

void 
ScubaLayer2DMRI::StartLine( float iRAS[3] ) {

  // Create a new line and set its beginning and ending to this point.
  mCurrentLine = new Line();
  mCurrentLine->mEndRAS[0] = mCurrentLine->mBeginRAS[0] = iRAS[0];
  mCurrentLine->mEndRAS[1] = mCurrentLine->mBeginRAS[1] = iRAS[1];
  mCurrentLine->mEndRAS[2] = mCurrentLine->mBeginRAS[2] = iRAS[2];
}

void 
ScubaLayer2DMRI::StretchCurrentLine( float iRAS[3] ) {

  // Just update the current line endings.
  if( mCurrentLine ) {
    mCurrentLine->mEndRAS[0] = iRAS[0];
    mCurrentLine->mEndRAS[1] = iRAS[1];
    mCurrentLine->mEndRAS[2] = iRAS[2];
  }
}

void 
ScubaLayer2DMRI::EndLine( float iRAS[3], 
			  ScubaWindowToRASTranslator& iTranslator ) {

  if( mCurrentLine ) {

    // Set the line ending.
    mCurrentLine->mEndRAS[0] = iRAS[0];
    mCurrentLine->mEndRAS[1] = iRAS[1];
    mCurrentLine->mEndRAS[2] = iRAS[2];

    // Push this onto our list of lines.
    mLines.push_back( mCurrentLine );
    

    // Get window coords for our line beginning and ending. Get a list
    // of window points between those two points. For each one,
    // translate it back to an RAS and then tell the volume to mark it
    // as an edge.
    int fromWindow[2];
    int toWindow[2];
    iTranslator.TranslateRASToWindow( mCurrentLine->mBeginRAS, fromWindow );
    iTranslator.TranslateRASToWindow( mCurrentLine->mEndRAS, toWindow );
    
    list<Point2<int> > points;
    Utilities::FindPointsOnLine2d( fromWindow, toWindow, 1, points );
    
    list<Point2<int> >::iterator tPoints;
    for( tPoints = points.begin(); tPoints != points.end(); ++tPoints ) {
      
      Point2<int>& point = *tPoints;
      int index[2];
      index[0] = point.x();
      index[1] = point.y();
      float ras[3];
      iTranslator.TranslateWindowToRAS( index, ras );
      mVolume->MarkRASEdge( ras );
    }

    // Clear the current line.
    mCurrentLine = NULL;
  }
}


void 
ScubaLayer2DMRI::StartEdgeLine( float iRAS[3] ) {

  // Create a new line and set its beginning and ending to this point.
  mCurrentSnakeLine = new SnakeLine( iRAS );
}

void 
ScubaLayer2DMRI::StretchCurrentEdgeLine( float iRAS[3], 
					 ViewState& iViewState,
				  ScubaWindowToRASTranslator& iTranslator ) {

  if( mCurrentSnakeLine ) {

    
    EdgePathFinder finder( iViewState.mBufferWidth, iViewState.mBufferHeight,
			   (int)mVolume->GetMRIMagnitudeMaxValue(),
			   &iTranslator, mVolume );
    //    finder.SetOutputStreamToCerr();

    Point3<float> beginRAS( mCurrentSnakeLine->mBeginRAS );
    Point3<float> endRAS( iRAS );
    Point2<int> beginWindow;
    Point2<int> endWindow;
    iTranslator.TranslateRASToWindow( beginRAS.xyz(), beginWindow.xy() );
    iTranslator.TranslateRASToWindow( endRAS.xyz(), endWindow.xy() );
    list<Point2<int> > windowPoints;

    finder.FindPath( beginWindow, endWindow, windowPoints );

    mCurrentSnakeLine->mPointsRAS.clear();

    list<Point2<int> >::iterator tWindowPoint;
    for( tWindowPoint = windowPoints.begin();
	 tWindowPoint != windowPoints.end();
	 ++tWindowPoint ) {

      Point3<float> currentRAS;
      iTranslator.TranslateWindowToRAS( (*tWindowPoint).xy(), 
					currentRAS.xyz() );
      mCurrentSnakeLine->mPointsRAS.push_back( currentRAS );
    }
  }
}

void 
ScubaLayer2DMRI::EndEdgeLine( float iRAS[3], 
			  ScubaWindowToRASTranslator& iTranslator ) {

  if( mCurrentSnakeLine ) {
    
    // Add this line.
    mSnakeLines.push_back( mCurrentSnakeLine );

    // Clear the current line.
    mCurrentSnakeLine = NULL;
  }
}

void
ScubaLayer2DMRI::GetPreferredInPlaneIncrements ( float oIncrements[3] ) {
  
  oIncrements[0] = mVolume->GetVoxelXSize();
  oIncrements[1] = mVolume->GetVoxelYSize();
  oIncrements[2] = mVolume->GetVoxelZSize();
}

void
ScubaLayer2DMRI::DrawStraightLineIntoBuffer ( GLubyte* iBuffer, 
					      int iWidth, int iHeight,
					      ViewState& iViewState,
				       ScubaWindowToRASTranslator& iTranslator,
					      Line* iLine ) {
  
  int lineBegin[2];
  int lineEnd[2];
  int color[3];
  color[0] = 0; color[1] = 0; color[2] = 255;
  iTranslator.TranslateRASToWindow( iLine->mBeginRAS, lineBegin );
  iTranslator.TranslateRASToWindow( iLine->mEndRAS, lineEnd );
  DrawLineIntoBuffer( iBuffer, iWidth, iHeight, lineBegin, lineEnd,
		      color, 1, 1 );
}

void
ScubaLayer2DMRI::DrawSnakeLineIntoBuffer ( GLubyte* iBuffer, 
					   int iWidth, int iHeight,
					   ViewState& iViewState,
				      ScubaWindowToRASTranslator& iTranslator,
					   SnakeLine* iLine ) {

  bool bFirstPoint = true;
  Point3<float>* pointRASA = NULL;
  Point3<float>* pointRASB = NULL;
  int windowA[2], windowB[2];
  int color[3] = {255, 0, 0};
  
  list<Point3<float> >::iterator tPoint;
  for( tPoint = iLine->mPointsRAS.begin();
       tPoint != iLine->mPointsRAS.end();
       ++tPoint ) {
    if( bFirstPoint ) {
      pointRASA = &(*tPoint);
      bFirstPoint = false;
    } else {
      pointRASB = &(*tPoint);
      iTranslator.TranslateRASToWindow( pointRASA->xyz(), windowA );
      iTranslator.TranslateRASToWindow( pointRASB->xyz(), windowB );
      DrawLineIntoBuffer( iBuffer, iWidth, iHeight, windowA, windowB,
			  color, 1, 1 );
      pointRASA = pointRASB;
    }
  }
}


ScubaLayer2DMRIFloodSelect::ScubaLayer2DMRIFloodSelect ( bool ibSelect ) {
  mbSelect = ibSelect;
  mbFloodDlogOpen = false;
}


void
ScubaLayer2DMRIFloodSelect::DoBegin () {
      
  // Start the timer. If it goes past 2 seconds we'll open a dialog box.
  mFloodTimer.Start();
  mbFloodDlogOpen = false;

  // Start our undo action.
  UndoManager& undoList = UndoManager::GetManager();

  if( mbSelect ) {
    undoList.BeginAction( "Selection Fill" );
  } else {
    undoList.BeginAction( "Unselection Fill" );
  }

}

void 
ScubaLayer2DMRIFloodSelect::DoEnd () {

  // If the dialog is open, close it.
  if( mbFloodDlogOpen ) {
    
    TclDlogManager& manager = TclDlogManager::GetManager();
    manager.CloseDlog();
    mbFloodDlogOpen = false;
  }

  // End our undo action.
  UndoManager& undoList = UndoManager::GetManager();
  undoList.EndAction();
}

bool
ScubaLayer2DMRIFloodSelect::DoStopRequested () {

  // If the dialog is open, check for the stop button.
  if( mbFloodDlogOpen ) {
    
    TclDlogManager& manager = TclDlogManager::GetManager();
    int nButton = manager.CheckDlogForButton();
    if( nButton == 0 ) {
      return true;
    } 
  } 

  return false;
}

bool
ScubaLayer2DMRIFloodSelect::CompareVoxel ( float iRAS[3] ) {

  // Use this to check how much time the flood is taking. If over two
  // seconds, open a dialog box.
  if( !mbFloodDlogOpen &&
      mFloodTimer.TimeNow() > 2000 ) {
    
    TclDlogManager& manager = TclDlogManager::GetManager();
    list<string> lButtons;
    lButtons.push_back( "Stop" );

    if( mbSelect ) {
      manager.NewDlog( "Selecting", "Selecting voxels", false, lButtons );
    } else {
      manager.NewDlog( "Unselecting", "Unselecting voxels", false, lButtons );
    }

    mbFloodDlogOpen = true;
  }

  // Always return true.
  return true;
}

void
ScubaLayer2DMRIFloodSelect::DoVoxel ( float iRAS[3] ) {
  UndoManager& undoList = UndoManager::GetManager();

  if( mbSelect ) {
    mVolume->SelectRAS( iRAS );
    UndoSelectionAction* action = 
      new UndoSelectionAction( mVolume, true, iRAS );
    undoList.AddAction( action );
  } else {
    mVolume->UnselectRAS( iRAS );
    UndoSelectionAction* action =
      new UndoSelectionAction( mVolume, false, iRAS );
    undoList.AddAction( action );
  }
}

UndoSelectionAction::UndoSelectionAction ( VolumeCollection* iVolume,
					   bool ibSelect, float iRAS[3] ) {
  mVolume = iVolume;
  mbSelect = ibSelect;
  mRAS[0] = iRAS[0];
  mRAS[1] = iRAS[1];
  mRAS[2] = iRAS[2];
}

void
UndoSelectionAction::Undo () {
  if( mbSelect ) {
    mVolume->UnselectRAS( mRAS );
  } else {
    mVolume->SelectRAS( mRAS );
  }
}

void
UndoSelectionAction::Redo () {
  if( mbSelect ) {
    mVolume->SelectRAS( mRAS );
  } else {
    mVolume->UnselectRAS( mRAS );
  }
}


EdgePathFinder::EdgePathFinder ( int iViewWidth, int iViewHeight, 
				 int iLongestEdge,
				 ScubaWindowToRASTranslator* iTranslator,
				 VolumeCollection* iVolume ) {

  SetDimensions( iViewWidth, iViewHeight, iLongestEdge );
  mVolume = iVolume;
  mTranslator = iTranslator;
}
		  
float 
EdgePathFinder::GetEdgeCost ( Point2<int>& iPoint ) {

  // Get the magnitude value at this point. We add 0.1 to it because
  // if it's 0, there's no preference for straight lines, since
  // diagonal lines will have the same cost, which in some cases makes
  // a really weird looking line.
  float RAS[3];
  mTranslator->TranslateWindowToRAS( iPoint.xy(), RAS );
  return mVolume->GetMRIMagnitudeValueAtRAS( RAS ) + 0.1;

}
