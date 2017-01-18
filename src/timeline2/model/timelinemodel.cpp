/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/


#include "timelinemodel.hpp"
#include "trackmodel.hpp"
#include "clipmodel.hpp"

#include <mlt++/MltTractor.h>

TimelineModel::TimelineModel() :
    m_tractor()
{
}

TimelineModel::~TimelineModel()
{
    for(auto tracks : m_iteratorTable) {
        deleteTrackById(tracks.first);
    }
}


int TimelineModel::getTracksNumber()
{
    int count = m_tractor.count();
    Q_ASSERT(count >= 0);
    Q_ASSERT(count == static_cast<int>(m_allTracks.size()));
    return count;
}

void TimelineModel::deleteTrackById(int id)
{
    Q_ASSERT(m_iteratorTable.count(id) > 0);
    auto it = m_iteratorTable[id];
    (*it)->destruct();
}


void TimelineModel::registerTrack(std::unique_ptr<TrackModel>&& track, int pos)
{
    int id = track->getId();
    if (pos == -1) {
        pos = static_cast<int>(m_allTracks.size());
    }
    Q_ASSERT(pos >= 0);
    Q_ASSERT(pos <= static_cast<int>(m_allTracks.size()));
 
    //effective insertion (MLT operation)
    int error = m_tractor.insert_track(*track ,pos);
    Q_ASSERT(error == 0); //we might need better error handling...

    // we now insert in the list
    auto posIt = m_allTracks.begin();
    std::advance(posIt, pos);
    auto it = m_allTracks.insert(posIt, std::move(track));
    //it now contains the iterator to the inserted element, we store it
    Q_ASSERT(m_iteratorTable.count(id) == 0); //check that id is not used (shouldn't happen)
    m_iteratorTable[id] = it;
}


void TimelineModel::deregisterTrack(int id)
{
    auto it = m_iteratorTable[id]; //iterator to the element
    m_iteratorTable.erase(id);  //clean table
    auto index = std::distance(m_allTracks.begin(), it); //compute index in list
    m_tractor.remove_track(static_cast<int>(index)); //melt operation
    m_allTracks.erase(it);  //actual deletion of object
}
