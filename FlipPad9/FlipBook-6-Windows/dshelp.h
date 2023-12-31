
template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

// Query whether a pin is connected to another pin.
//
// Note: This function does not return a pointer to the connected pin.

HRESULT IsPinConnected(IPin *pPin, BOOL *pResult)
{
    IPin *pTmp = NULL;
    HRESULT hr = pPin->ConnectedTo(&pTmp);
    if (SUCCEEDED(hr))
    {
        *pResult = TRUE;
    }
    else if (hr == VFW_E_NOT_CONNECTED)
    {
        // The pin is not connected. This is not an error for our purposes.
        *pResult = FALSE;
        hr = S_OK;
    }

    SafeRelease(&pTmp);
    return hr;
}



// Query whether a pin has a specified direction (input / output)
HRESULT IsPinDirection(IPin *pPin, PIN_DIRECTION dir, BOOL *pResult)
{
    PIN_DIRECTION pinDir;
    HRESULT hr = pPin->QueryDirection(&pinDir);
    if (SUCCEEDED(hr))
    {
        *pResult = (pinDir == dir);
    }
    return hr;
}


// Match a pin by pin direction and connection state.
HRESULT MatchPin(IPin *pPin, PIN_DIRECTION direction, BOOL bShouldBeConnected,
BOOL *pResult)
{
    ASSERT(pResult != NULL);

    BOOL bMatch = FALSE;
    BOOL bIsConnected = FALSE;

    HRESULT hr = IsPinConnected(pPin, &bIsConnected);
    if (SUCCEEDED(hr))
    {
        if (bIsConnected == bShouldBeConnected)
        {
            hr = IsPinDirection(pPin, direction, &bMatch);
        }
    }

    if (SUCCEEDED(hr))
    {
        *pResult = bMatch;
    }
    return hr;
}

// Return the first unconnected input pin or output pin.
HRESULT FindUnconnectedPin(IBaseFilter *pFilter, PIN_DIRECTION PinDir, IPin
**ppPin)
{
    IEnumPins *pEnum = NULL;
    IPin *pPin = NULL;
    BOOL bFound = FALSE;

    HRESULT hr = pFilter->EnumPins(&pEnum);
    if (FAILED(hr))
    {
        goto done;
    }

    while (S_OK == pEnum->Next(1, &pPin, NULL))
    {
        hr = MatchPin(pPin, PinDir, FALSE, &bFound);
        if (FAILED(hr))
        {
            goto done;
        }
        if (bFound)
        {
            *ppPin = pPin;
            (*ppPin)->AddRef();
            break;
        }
        SafeRelease(&pPin);
    }

    if (!bFound)
    {
        hr = VFW_E_NOT_FOUND;
    }

done:
    SafeRelease(&pPin);
    SafeRelease(&pEnum);
    return hr;
}







HRESULT ConnectFilters(
    IGraphBuilder *pGraph, // Filter Graph Manager.
    IPin *pOut,            // Output pin on the upstream filter.
    IBaseFilter *pDest)    // Downstream filter.
{
    IPin *pIn = NULL;
        
    // Find an input pin on the downstream filter.
    HRESULT hr = FindUnconnectedPin(pDest, PINDIR_INPUT, &pIn);
    if (SUCCEEDED(hr))
    {
        // Try to connect them.
        hr = pGraph->Connect(pOut, pIn);
        pIn->Release();
    }
    return hr;
}


HRESULT ConnectFilters(IGraphBuilder *pGraph, IBaseFilter *pSrc, IPin *pIn)
{
    IPin *pOut = NULL;
        
    // Find an output pin on the upstream filter.
    HRESULT hr = FindUnconnectedPin(pSrc, PINDIR_OUTPUT, &pOut);
    if (SUCCEEDED(hr))
    {
        // Try to connect them.
        hr = pGraph->Connect(pOut, pIn);
        pOut->Release();
    }
    return hr;
}

HRESULT ConnectFilters(IGraphBuilder *pGraph, IBaseFilter *pSrc, IBaseFilter
*pDest)
{
    IPin *pOut = NULL;

    // Find an output pin on the first filter.
    HRESULT hr = FindUnconnectedPin(pSrc, PINDIR_OUTPUT, &pOut);
    if (SUCCEEDED(hr))
    {
        hr = ConnectFilters(pGraph, pOut, pDest);
        pOut->Release();
    }
    return hr;
}

//===========================================================================
// Note - on error, returns false instead of HRESULT.  Others may want to
// modify that ...

// This function optionally returns the address of the IPin through ppPin


//===========================================================================

bool filterHasCompatibleOutputPin (IBaseFilter  *pFilter,
                                   GUID         *mediaType,
                                   IPin         **ppPin)     // can be NULL
{
   IEnumPins         *pPinEnum = NULL;
   IEnumMediaTypes   *pMtypeEnum = NULL;
   IPin              *pPin = NULL;
   PIN_DIRECTION     pinDir;
   AM_MEDIA_TYPE     *amMediaType = NULL;
   unsigned long     cFetched;
   HRESULT           hr = S_OK;
   bool              retval = false;

   //========================================================================
   // get the enumeration list of pins on the passed filter and loop on them
   //========================================================================

   if (SUCCEEDED(hr = pFilter->EnumPins (&pPinEnum)))
   {
      while ((hr = pPinEnum->Next(1, &pPin, &cFetched)) == S_OK)
      {
         //===============================================================
         // Query for info - get direction and media types
         //===============================================================

         pPin->QueryDirection (&pinDir);
         if (pinDir == PINDIR_OUTPUT)
         {
            if (SUCCEEDED(hr = pPin->EnumMediaTypes (&pMtypeEnum)))
            {
               while ((hr = pMtypeEnum->Next (1, &amMediaType, &cFetched)) == S_OK)
               {
                  if (amMediaType->majortype == *mediaType)
                     retval = true;

                  DeleteMediaType (amMediaType);
                  if (retval == true)
                     break;
               }
            }
         }

         //===============================================================
         // if we found appropriate pin, decide whether to return it or
         // release it.  Otherwise, release it always.
         //===============================================================

         if (retval == true)
         {
            if (ppPin != NULL)
               *ppPin = pPin;
            else
               pPin->Release();
            break;
         }
         else
            pPin->Release();
      }
   }

   return retval;
}



