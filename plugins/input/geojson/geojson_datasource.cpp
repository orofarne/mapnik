/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2014 Artem Pavlenko
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

#include "geojson_datasource.hpp"
#include "geojson_featureset.hpp"
#include "large_geojson_featureset.hpp"
#include <fstream>
#include <algorithm>

// boost
#include <boost/algorithm/string.hpp>
#include <boost/spirit/include/qi.hpp>

// mapnik
#include <mapnik/boolean.hpp>
#include <mapnik/unicode.hpp>
#include <mapnik/utils.hpp>
#include <mapnik/feature.hpp>
#include <mapnik/feature_factory.hpp>
#include <mapnik/feature_kv_iterator.hpp>
#include <mapnik/value_types.hpp>
#include <mapnik/box2d.hpp>
#include <mapnik/debug.hpp>
#include <mapnik/proj_transform.hpp>
#include <mapnik/projection.hpp>
#include <mapnik/util/geometry_to_ds_type.hpp>
#include <mapnik/util/variant.hpp>
#include <mapnik/util/file_io.hpp>
#include <mapnik/make_unique.hpp>
#include <mapnik/json/feature_collection_grammar.hpp>
#include <mapnik/json/extract_bounding_box_grammar_impl.hpp>
#include <mapnik/util/boost_geometry_adapters.hpp> // boost.geometry - register box2d<double>

using mapnik::datasource;
using mapnik::parameters;

DATASOURCE_PLUGIN(geojson_datasource)

struct attr_value_converter
{
    mapnik::eAttributeType operator() (mapnik::value_integer) const
    {
        return mapnik::Integer;
    }

    mapnik::eAttributeType operator() (double) const
    {
        return mapnik::Double;
    }

    mapnik::eAttributeType operator() (float) const
    {
        return mapnik::Double;
    }

    mapnik::eAttributeType operator() (bool) const
    {
        return mapnik::Boolean;
    }

    mapnik::eAttributeType operator() (std::string const& ) const
    {
        return mapnik::String;
    }

    mapnik::eAttributeType operator() (mapnik::value_unicode_string const&) const
    {
        return mapnik::String;
    }

    mapnik::eAttributeType operator() (mapnik::value_null const& ) const
    {
        return mapnik::String;
    }
};

geojson_datasource::geojson_datasource(parameters const& params)
  : datasource(params),
    type_(datasource::Vector),
    desc_(geojson_datasource::name(),
              *params.get<std::string>("encoding","utf-8")),
    filename_(),
    inline_string_(),
    extent_(),
    features_(),
    tree_(nullptr)
{
    boost::optional<std::string> inline_string = params.get<std::string>("inline");
    if (inline_string)
    {
        inline_string_ = *inline_string;
    }
    else
    {
        boost::optional<std::string> file = params.get<std::string>("file");
        if (!file) throw mapnik::datasource_exception("GeoJSON Plugin: missing <file> parameter");

        boost::optional<std::string> base = params.get<std::string>("base");
        if (base)
            filename_ = *base + "/" + *file;
        else
            filename_ = *file;
    }
    if (!inline_string_.empty())
    {
        parse_geojson(inline_string_);
    }
    else
    {

        mapnik::util::file file(filename_);
        if (!file.open())
        {
            throw mapnik::datasource_exception("GeoJSON Plugin: could not open: '" + filename_ + "'");
        }

        std::string file_buffer;
        file_buffer.resize(file.size());
        std::fread(&file_buffer[0], file.size(), 1, file.get());
        cache_features_ = *params.get<mapnik::boolean_type>("cache_features", true);
        if (cache_features_)
        {
            parse_geojson(file_buffer);
        }
        else
        {
            initialise_index(file_buffer.begin(), file_buffer.end());
        }
    }
}

namespace {
using base_iterator_type = std::string::const_iterator;
const mapnik::transcoder tr("utf8");
const mapnik::json::feature_collection_grammar<base_iterator_type,mapnik::feature_impl> fc_grammar(tr);
}

template <typename Iterator>
void geojson_datasource::initialise_index(Iterator start, Iterator end)
{
    mapnik::json::boxes boxes;
    mapnik::json::extract_bounding_box_grammar<Iterator> bbox_grammar;
    boost::spirit::standard_wide::space_type space;
    if (!boost::spirit::qi::phrase_parse(start, end, (bbox_grammar)(boost::phoenix::ref(boxes)) , space))
    {
        throw mapnik::datasource_exception("GeoJSON Plugin: could not parse: '" + filename_ + "'");
    }
#if BOOST_VERSION >= 105600
    tree_ = std::make_unique<spatial_index_type>(boxes);
#else
    tree_ = std::make_unique<spatial_index_type>(16, 4);
#endif
    for (auto const& item : boxes)
    {
        auto const& box = std::get<0>(item);
        auto const& geometry_index = std::get<1>(item);
#if BOOST_VERSION < 105600
        tree_->insert(box, geometry_index);
#endif
        if (!extent_.valid())
        {
            extent_ = box;
            // parse first feature to extract attributes schema.
            // NOTE: this doesn't yield correct answer for geoJSON in general, just an indication
            mapnik::util::file file(filename_);
            if (!file.open())
            {
                throw mapnik::datasource_exception("GeoJSON Plugin: could not open: '" + filename_ + "'");
            }

            std::fseek(file.get(), geometry_index.first, SEEK_SET);
            std::vector<char> json;
            json.resize(geometry_index.second);
            std::fread(json.data(), geometry_index.second, 1, file.get());

            using chr_iterator_type = std::vector<char>::const_iterator;
            chr_iterator_type start = json.begin();
            chr_iterator_type end = json.end();

            mapnik::context_ptr ctx = std::make_shared<mapnik::context_type>();
            mapnik::feature_ptr feature(mapnik::feature_factory::create(ctx,1));
            using namespace boost::spirit;
            static const mapnik::transcoder tr("utf8");
            static const mapnik::json::feature_grammar<chr_iterator_type,mapnik::feature_impl> grammar(tr);
            standard_wide::space_type space;
            if (!qi::phrase_parse(start, end, (grammar)(boost::phoenix::ref(*feature)), space))
            {
                throw std::runtime_error("Failed to parse geojson feature");
            }
            for ( auto const& kv : *feature)
            {
                desc_.add_descriptor(mapnik::attribute_descriptor(std::get<0>(kv),
                                                                  mapnik::util::apply_visitor(attr_value_converter(),
                                                                                              std::get<1>(kv))));
            }
        }
        else
        {
            extent_.expand_to_include(box);
        }
    }
}

template <typename T>
void geojson_datasource::parse_geojson(T const& buffer)
{
    boost::spirit::standard_wide::space_type space;
    mapnik::context_ptr ctx = std::make_shared<mapnik::context_type>();
    std::size_t start_id = 1;
    bool result = boost::spirit::qi::phrase_parse(buffer.begin(), buffer.end(), (fc_grammar)
                                                  (boost::phoenix::ref(ctx),boost::phoenix::ref(start_id)),
                                                  space, features_);
    if (!result)
    {
        if (!inline_string_.empty()) throw mapnik::datasource_exception("geojson_datasource: Failed parse GeoJSON file from in-memory string");
        else throw mapnik::datasource_exception("geojson_datasource: Failed parse GeoJSON file '" + filename_ + "'");
    }

#if BOOST_VERSION >= 105600
    using values_container = std::vector< std::pair<box_type, std::pair<std::size_t, std::size_t>>>;
    values_container values;
    values.reserve(features_.size());
#else
    tree_ = std::make_unique<spatial_index_type>(16, 4);
#endif

    std::size_t geometry_index = 0;
    for (mapnik::feature_ptr const& f : features_)
    {
        mapnik::box2d<double> box = f->envelope();
        if (box.valid())
        {
            if (geometry_index == 0)
            {
                extent_ = box;
                for ( auto const& kv : *f)
                {
                    desc_.add_descriptor(mapnik::attribute_descriptor(std::get<0>(kv),
                                                                      mapnik::util::apply_visitor(attr_value_converter(),
                                                                                                  std::get<1>(kv))));
                }
            }
            else
            {
                extent_.expand_to_include(box);
            }
        }
#if BOOST_VERSION >= 105600
        values.emplace_back(box, std::make_pair(geometry_index,0));
#else
        tree_->insert(box, std::make_pair(geometry_index));
#endif
        ++geometry_index;
    }

#if BOOST_VERSION >= 105600
    // packing algorithm
    tree_ = std::make_unique<spatial_index_type>(values);
#endif

}

geojson_datasource::~geojson_datasource() { }

const char * geojson_datasource::name()
{
    return "geojson";
}

boost::optional<mapnik::datasource::geometry_t> geojson_datasource::get_geometry_type() const
{
    boost::optional<mapnik::datasource::geometry_t> result;
    int multi_type = 0;
    unsigned num_features = features_.size();
    for (unsigned i = 0; i < num_features && i < 5; ++i)
    {
        mapnik::util::to_ds_type(features_[i]->paths(),result);
        if (result)
        {
            int type = static_cast<int>(*result);
            if (multi_type > 0 && multi_type != type)
            {
                result.reset(mapnik::datasource::Collection);
                return result;
            }
            multi_type = type;
        }
    }
    return result;
}

mapnik::datasource::datasource_t geojson_datasource::type() const
{
    return type_;
}

mapnik::box2d<double> geojson_datasource::envelope() const
{
    return extent_;
}

mapnik::layer_descriptor geojson_datasource::get_descriptor() const
{
    return desc_;
}

mapnik::featureset_ptr geojson_datasource::features(mapnik::query const& q) const
{
    // if the query box intersects our world extent then query for features
    mapnik::box2d<double> const& box = q.get_bbox();
    if (extent_.intersects(box))
    {
#if BOOST_VERSION >= 105600
        geojson_featureset::array_type index_array;
        if (tree_)
        {
            tree_->query(boost::geometry::index::intersects(box),std::back_inserter(index_array));

            if (cache_features_)
            {
                return std::make_shared<geojson_featureset>(features_, std::move(index_array));
            }
            else
            {
                std::sort(index_array.begin(),index_array.end(),
                          [] (item_type const& item0, item_type const& item1)
                          {
                              return item0.second.first < item1.second.first;
                          });
                return std::make_shared<large_geojson_featureset>(filename_, std::move(index_array));
            }
        }
#else
        if (tree_)
        {
            if (cache_features_)
                return std::make_shared<geojson_featureset>(features_, tree_->find(box));
            else
                return std::make_shared<large_geojson_featureset>(features_, tree_->find(box));
        }
#endif
    }
    // otherwise return an empty featureset pointer
    return mapnik::featureset_ptr();
}

mapnik::featureset_ptr geojson_datasource::features_at_point(mapnik::coord2d const& pt, double tol) const
{
    mapnik::box2d<double> query_bbox(pt, pt);
    query_bbox.pad(tol);
    mapnik::query q(query_bbox);
    std::vector<mapnik::attribute_descriptor> const& desc = desc_.get_descriptors();
    std::vector<mapnik::attribute_descriptor>::const_iterator itr = desc.begin();
    std::vector<mapnik::attribute_descriptor>::const_iterator end = desc.end();
    for ( ;itr!=end;++itr)
    {
        q.add_property_name(itr->get_name());
    }
    return features(q);
}
