/***************************************************************************
                          doctrackdecorator  -  description
                             -------------------
    begin                : Wed Jan 7 2004
    copyright            : (C) 2004 by Jason Wood
    email                : jasonwood@blueyonder.co.uk
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "doctrackdecorator.h"

#include "gentime.h"
#include "doctrackbase.h"
#include "kdenlivedoc.h"
#include "ktimeline.h"

DocTrackDecorator::DocTrackDecorator(KTimeLine *timeline, KdenliveDoc *doc) :
							TrackViewDecorator(timeline),
							m_document(doc)
{
}

DocTrackDecorator::~DocTrackDecorator()
{
}
