/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2011 Artem Pavlenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

#ifndef MAPNIK_RASTER_HPP
#define MAPNIK_RASTER_HPP

// mapnik
#include <mapnik/box2d.hpp>
#include <mapnik/image_data.hpp>
#include <mapnik/graphics.hpp>
#include <mapnik/noncopyable.hpp>

// boost
#include <boost/shared_ptr.hpp>

namespace mapnik {
class raster : private mapnik::noncopyable
{
public:
    box2d<double> ext_;
    image_data_32 data_;
    bool premultiplied_alpha_;
    raster(box2d<double> const& ext, unsigned width, unsigned height, bool premultiplied_alpha = false)
        : ext_(ext),
          data_(width,height),
          premultiplied_alpha_(premultiplied_alpha)
    {}

    raster(boost::shared_ptr<image_32> im, box2d<double> const& ext)
        : ext_(ext),
          data_(im->width(),im->height(),im->data().getData()),
          premultiplied_alpha_(im->premultiplied())
    {}
};
}

#endif // MAPNIK_RASTER_HPP
