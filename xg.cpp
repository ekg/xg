#include "xg.hpp"
#include "stream.hpp"

namespace xg {

int dna3bit(char c) {
    switch (c) {
    case 'A':
        return 0;
    case 'T':
        return 1;
    case 'C':
        return 2;
    case 'G':
        return 3;
    default:
        return 4;
    }
}

char revdna3bit(int i) {
    switch (i) {
    case 0:
        return 'A';
    case 1:
        return 'T';
    case 2:
        return 'C';
    case 3:
        return 'G';
    default:
        return 'N';
    }
}

XG::XG(istream& in)
    : start_marker('#'),
      end_marker('$'),
      seq_length(0),
      node_count(0),
      edge_count(0),
      path_count(0) {
    load(in);
}

XG::XG(Graph& graph)
    : start_marker('#'),
      end_marker('$'),
      seq_length(0),
      node_count(0),
      edge_count(0),
      path_count(0) {
    from_graph(graph);
}

void XG::load(istream& in) {

    if (!in.good()) {
        cerr << "[xg] error: index does not exist!" << endl;
        exit(1);
    }

    sdsl::read_member(seq_length, in);
    sdsl::read_member(node_count, in);
    sdsl::read_member(edge_count, in);
    sdsl::read_member(path_count, in);
    size_t entity_count = node_count + edge_count;
    //cerr << sequence_length << ", " << node_count << ", " << edge_count << endl;
    sdsl::read_member(min_id, in);
    sdsl::read_member(max_id, in);

    i_iv.load(in);
    r_iv.load(in);

    s_iv.load(in);
    s_cbv.load(in);
    s_cbv_rank.load(in, &s_cbv);
    s_cbv_select.load(in, &s_cbv);

    f_iv.load(in);
    f_bv.load(in);
    f_bv_rank.load(in, &f_bv);
    f_bv_select.load(in, &f_bv);
    f_from_start_cbv.load(in);
    f_to_end_cbv.load(in);

    t_iv.load(in);
    t_bv.load(in);
    t_bv_rank.load(in, &t_bv);
    t_bv_select.load(in, &t_bv);
    t_to_end_cbv.load(in);
    t_from_start_cbv.load(in);

    pn_iv.load(in);
    pn_csa.load(in);
    pn_bv.load(in);
    pn_bv_rank.load(in, &pn_bv);
    pn_bv_select.load(in, &pn_bv);
    pi_iv.load(in);
    sdsl::read_member(path_count, in);
    for (size_t i = 0; i < path_count; ++i) {
        auto path = new XGPath;
        path->load(in);
        paths.push_back(path);
    }
    ep_iv.load(in);
    ep_bv.load(in);
    ep_bv_rank.load(in, &ep_bv);
    ep_bv_select.load(in, &ep_bv);
}

void XGPath::load(istream& in) {
    members.load(in);
    ids.load(in);
    directions.load(in);
    positions.load(in);
    offsets.load(in);
    offsets_rank.load(in, &offsets);
    offsets_select.load(in, &offsets);
}

size_t XGPath::serialize(std::ostream& out,
                         sdsl::structure_tree_node* v,
                         std::string name) {
    sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
    size_t written = 0;
    written += members.serialize(out, child, "path_membership_" + name);
    written += ids.serialize(out, child, "path_node_ids_" + name);
    written += directions.serialize(out, child, "path_node_directions_" + name);
    written += positions.serialize(out, child, "path_node_offsets_" + name);
    written += offsets.serialize(out, child, "path_node_starts_" + name);
    written += offsets_rank.serialize(out, child, "path_node_starts_rank_" + name);
    written += offsets_select.serialize(out, child, "path_node_starts_select_" + name);
    return written;
}

XGPath::XGPath(const string& path_name,
               const vector<Traversal>& path,
               size_t entity_count,
               XG& graph,
               const map<int64_t, string>& node_label) {

    name = path_name;
    member_count = 0;
    
    // path members (of nodes and edges ordered as per f_bv)
    bit_vector members_bv;
    util::assign(members_bv, bit_vector(entity_count));
    // node ids, the literal path
    int_vector<> ids_iv;
    util::assign(ids_iv, int_vector<>(path.size()));
    // directions of traversal (typically forward, but we allow backwards
    bit_vector directions_bv;
    util::assign(directions_bv, bit_vector(path.size()));
    // node positions in path
    util::assign(positions, int_vector<>(path.size()));

    size_t path_off = 0;
    size_t members_off = 0;
    size_t positions_off = 0;
    size_t path_length = 0;

    // determine total length
    for (size_t i = 0; i < path.size(); ++i) {
        auto& node_id = path[i].id;
        auto label_itr = node_label.find(node_id);
        if (label_itr != node_label.end()) {
            path_length += label_itr->second.size();
            ids_iv[i] = graph.id_to_rank(node_id);
        } else {
            cerr << "[xg] error: when making paths could not find node label for " << node_id << endl;
            assert(false);
        }
    }

    // make the bitvector for path offsets
    util::assign(offsets, bit_vector(path_length));
    for (size_t i = 0; i < path.size(); ++i) {
        //cerr << i << endl;
        auto& node_id = path[i].id;
        auto direction = path[i].rev;
        //cerr << node_id << endl;
        // record node
        members_bv[graph.node_rank_as_entity(node_id)-1] = 1;
        // record direction of passage through node
        directions_bv[i] = direction;
        // we've seen another entity
        ++member_count;
        // and record node offset in path
        positions[positions_off++] = path_off;
        // record position of node
        offsets[path_off] = 1;
        // and update the offset counter
        auto label_itr = node_label.find(node_id);
        if (label_itr != node_label.end()) {
            path_off += label_itr->second.size();
        } else {
            cerr << "[xg] error: when recording offsets could not find node label for " << node_id << endl;
            assert(false);
        }

        // find the next edge in the path, and record it
        if (i+1 < path.size()) { // but only if there is a next node
            auto& next_node_id = path[i+1].id;
            if (graph.has_edge(node_id, next_node_id)) {
                members_bv[graph.edge_rank_as_entity(node_id, next_node_id)-1] = 1;
                // TODO should we record directions for edges?
                // why would we?
                ++member_count;
            }
        }
    }
    // compress path membership vectors
    util::assign(members, sd_vector<>(members_bv));
    // and traversal information
    util::assign(directions, sd_vector<>(directions_bv));
    // handle entity lookup structure (wavelet tree)
    util::bit_compress(ids_iv);
    construct_im(ids, ids_iv);
    // bit compress the positional offset info
    util::bit_compress(positions);

    util::assign(offsets_rank, rank_support_v<1>(&offsets));
    util::assign(offsets_select, bit_vector::select_1_type(&offsets));
}

size_t XG::serialize(ostream& out, sdsl::structure_tree_node* s, std::string name) {

    sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(s, name, sdsl::util::class_name(*this));
    size_t written = 0;

    written += sdsl::write_member(s_iv.size(), out, child, "sequence_length");
    written += sdsl::write_member(i_iv.size(), out, child, "node_count");
    written += sdsl::write_member(f_iv.size()-i_iv.size(), out, child, "edge_count");
    written += sdsl::write_member(path_count, out, child, "path_count");
    written += sdsl::write_member(min_id, out, child, "min_id");
    written += sdsl::write_member(max_id, out, child, "max_id");

    written += i_iv.serialize(out, child, "id_rank_vector");
    written += r_iv.serialize(out, child, "rank_id_vector");

    written += s_iv.serialize(out, child, "seq_vector");
    written += s_cbv.serialize(out, child, "seq_node_starts");
    written += s_cbv_rank.serialize(out, child, "seq_node_starts_rank");
    written += s_cbv_select.serialize(out, child, "seq_node_starts_select");

    written += f_iv.serialize(out, child, "from_vector");
    written += f_bv.serialize(out, child, "from_node");
    written += f_bv_rank.serialize(out, child, "from_node_rank");
    written += f_bv_select.serialize(out, child, "from_node_select");
    written += f_from_start_cbv.serialize(out, child, "from_is_from_start");
    written += f_to_end_cbv.serialize(out, child, "from_is_to_end");
    
    written += t_iv.serialize(out, child, "to_vector");
    written += t_bv.serialize(out, child, "to_node");
    written += t_bv_rank.serialize(out, child, "to_node_rank");
    written += t_bv_select.serialize(out, child, "to_node_select");
    written += t_to_end_cbv.serialize(out, child, "to_is_to_end");
    written += t_from_start_cbv.serialize(out, child, "to_is_from_start");

    written += pn_iv.serialize(out, child, "path_names");
    written += pn_csa.serialize(out, child, "path_names_csa");
    written += pn_bv.serialize(out, child, "path_names_starts");
    written += pn_bv_rank.serialize(out, child, "path_names_starts_rank");
    written += pn_bv_select.serialize(out, child, "path_names_starts_select");
    written += pi_iv.serialize(out, child, "path_ids");
    written += sdsl::write_member(paths.size(), out, child, "path_count");    
    for (auto path : paths) {
        written += path->serialize(out, child, path->name);
    }
    
    written += ep_iv.serialize(out, child, "entity_path_mapping");
    written += ep_bv.serialize(out, child, "entity_path_mapping_starts");
    written += ep_bv_rank.serialize(out, child, "entity_path_mapping_starts_rank");
    written += ep_bv_select.serialize(out, child, "entity_path_mapping_starts_select");

    sdsl::structure_tree::add_size(child, written);
    return written;
    
}

void XG::from_stream(istream& in, bool validate_graph, bool print_graph) {

    // temporaries for construction
    map<int64_t, string> node_label;
    // need to store node sides
    map<Side, set<Side> > from_to;
    map<Side, set<Side> > to_from;
    map<string, vector<Traversal> > path_nodes;

    // we can always resize smaller, but not easily extend
    function<void(Graph&)> lambda = [this,
                                     &node_label,
                                     &from_to,
                                     &to_from,
                                     &path_nodes](Graph& graph) {
        for (int i = 0; i < graph.node_size(); ++i) {
            const Node& n = graph.node(i);
            if (node_label.find(n.id()) == node_label.end()) {
                ++node_count;
                seq_length += n.sequence().size();
                node_label[n.id()] = n.sequence();
            }
        }
        for (int i = 0; i < graph.edge_size(); ++i) {
            const Edge& e = graph.edge(i);
            if (from_to.find(Side(e.from(), e.from_start())) == from_to.end()
                || from_to[Side(e.from(), e.from_start())].count(Side(e.to(), e.to_end())) == 0) {
                ++edge_count;
                from_to[Side(e.from(), e.from_start())].insert(Side(e.to(), e.to_end()));
                to_from[Side(e.to(), e.to_end())].insert(Side(e.from(), e.from_start()));
            }
        }
        for (int i = 0; i < graph.path_size(); ++i) {
            const Path& p = graph.path(i);
            const string& name = p.name();
            for (int j = 0; j < p.mapping_size(); ++j) {
                const Mapping& m = p.mapping(j);
                path_nodes[name].push_back(Traversal(m.position().node_id(),
                                                     m.is_reverse()));
            }
        }
    };
    stream::for_each(in, lambda);
    path_count = path_nodes.size();

    build(node_label, from_to, to_from, path_nodes, validate_graph, print_graph);

}

void XG::from_graph(Graph& graph, bool validate_graph, bool print_graph) {

    // temporaries for construction
    map<int64_t, string> node_label;
    // need to store node sides
    map<Side, set<Side> > from_to;
    map<Side, set<Side> > to_from;
    map<string, vector<Traversal> > path_nodes;

    // we can always resize smaller, but not easily extend
    for (int i = 0; i < graph.node_size(); ++i) {
        const Node& n = graph.node(i);
        if (node_label.find(n.id()) == node_label.end()) {
            ++node_count;
            seq_length += n.sequence().size();
            node_label[n.id()] = n.sequence();
        }
    }
    for (int i = 0; i < graph.edge_size(); ++i) {
        const Edge& e = graph.edge(i);
        if (from_to.find(Side(e.from(), e.from_start())) == from_to.end()
            || from_to[Side(e.from(), e.from_start())].count(Side(e.to(), e.to_end())) == 0) {
            ++edge_count;
            from_to[Side(e.from(), e.from_start())].insert(Side(e.to(), e.to_end()));
            to_from[Side(e.to(), e.to_end())].insert(Side(e.from(), e.from_start()));
        }
    }
    for (int i = 0; i < graph.path_size(); ++i) {
        const Path& p = graph.path(i);
        const string& name = p.name();
        for (int j = 0; j < p.mapping_size(); ++j) {
            const Mapping& m = p.mapping(j);
            path_nodes[name].push_back(Traversal(m.position().node_id(),
                                                 m.is_reverse()));
        }
    }

    path_count = path_nodes.size();

    build(node_label, from_to, to_from, path_nodes, validate_graph, print_graph);

}

void XG::build(map<int64_t, string>& node_label,
               map<Side, set<Side> >& from_to,
               map<Side, set<Side> >& to_from,
               map<string, vector<Traversal> >& path_nodes,
               bool validate_graph,
               bool print_graph) {

    size_t entity_count = node_count + edge_count;
#ifdef VERBOSE_DEBUG
    cerr << "graph has " << seq_length << "bp in sequence, "
         << node_count << " nodes, "
         << edge_count << " edges, and "
         << path_count << " paths "
         << "for a total of " << entity_count << " entities" << endl;
#endif

    // for mapping of ids to ranks using a vector rather than wavelet tree
    min_id = node_label.begin()->first;
    max_id = node_label.rbegin()->first;
    
    // set up our compressed representation
    util::assign(s_iv, int_vector<>(seq_length, 0, 3));
    util::assign(s_bv, bit_vector(seq_length));
    util::assign(i_iv, int_vector<>(node_count));
    util::assign(r_iv, int_vector<>(max_id-min_id+1)); // note possibly discontiguous
    util::assign(f_iv, int_vector<>(entity_count));
    util::assign(f_bv, bit_vector(entity_count));
    util::assign(f_from_start_bv, bit_vector(entity_count));
    util::assign(f_to_end_bv, bit_vector(entity_count));
    util::assign(t_iv, int_vector<>(entity_count));
    util::assign(t_bv, bit_vector(entity_count));
    util::assign(t_to_end_bv, bit_vector(entity_count));
    util::assign(t_from_start_bv, bit_vector(entity_count));

    // for each node in the sequence
    // concatenate the labels into the s_iv
#ifdef VERBOSE_DEBUG
    cerr << "storing node labels" << endl;
#endif
    size_t i = 0; // insertion point
    size_t r = 1;
    for (auto& p : node_label) {
        int64_t id = p.first;
        const string& l = p.second;
        s_bv[i] = 1; // record node start
        i_iv[r-1] = id;
        // store ids to rank mapping
        r_iv[id-min_id] = r;
        ++r;
        for (auto c : l) {
            s_iv[i++] = dna3bit(c); // store sequence
        }
    }
    //node_label.clear();

    // we have to process all the nodes before we do the edges
    // because we need to ensure full coverage of node space

    util::bit_compress(i_iv);
    util::bit_compress(r_iv);

#ifdef VERBOSE_DEBUG    
    cerr << "storing forward edges and adjacency table" << endl;
#endif
    size_t f_itr = 0;
    size_t j_itr = 0; // edge adjacency pointer
    for (size_t k = 0; k < node_count; ++k) {
        int64_t f_id = i_iv[k];
        size_t f_rank = k+1;
        f_iv[f_itr] = f_rank;
        f_bv[f_itr] = 1;
        ++f_itr;
        for (auto end : { false, true }) {
            if (from_to.find(Side(f_id, end)) != from_to.end()) {
                auto t_side_itr = from_to.find(Side(f_id, end));
                if (t_side_itr != from_to.end()) {
                    for (auto& t_side : t_side_itr->second) {
                        size_t t_rank = id_to_rank(t_side.first);
                        // store link
                        f_iv[f_itr] = t_rank;
                        f_bv[f_itr] = 0;
                        // store side for start of edge
                        f_from_start_bv[f_itr] = end;
                        f_to_end_bv[f_itr] = t_side.second;
                        ++f_itr;
                    }
                }
            }
        }
    }

    // compress the forward direction side information
    util::assign(f_from_start_cbv, sd_vector<>(f_from_start_bv));
    util::assign(f_to_end_cbv, sd_vector<>(f_to_end_bv));
    
    //assert(e_iv.size() == edge_count*3);
#ifdef VERBOSE_DEBUG
    cerr << "storing reverse edges" << endl;
#endif

    size_t t_itr = 0;
    for (size_t k = 0; k < node_count; ++k) {
        //cerr << k << endl;
        int64_t t_id = i_iv[k];
        size_t t_rank = k+1;
        t_iv[t_itr] = t_rank;
        t_bv[t_itr] = 1;
        ++t_itr;
        for (auto end : { false, true }) {
            if (to_from.find(Side(t_id, end)) != to_from.end()) {
                auto f_side_itr = to_from.find(Side(t_id, end));
                if (f_side_itr != to_from.end()) {
                    for (auto& f_side : f_side_itr->second) {
                        size_t f_rank = id_to_rank(f_side.first);
                        // store link
                        t_iv[t_itr] = f_rank;
                        t_bv[t_itr] = 0;
                        // store side for end of edge
                        t_to_end_bv[t_itr] = end;
                        t_from_start_bv[t_itr] = f_side.second;
                        ++t_itr;
                    }
                }
            }
        }
    }

    // compress the reverse direction side information
    util::assign(t_to_end_cbv, sd_vector<>(t_to_end_bv));
    util::assign(t_from_start_cbv, sd_vector<>(t_from_start_bv));


    /*
    csa_wt<wt_int<rrr_vector<63>>> csa;
    int_vector<> x = {1,8,15,23,1,8,23,11,8};
    construct_im(csa, x, 8);
    cout << " i SA ISA PSI LF BWT   T[SA[i]..SA[i]-1]" << endl;
    csXprintf(cout, "%2I %2S %3s %3P %2p %3B   %:3T", csa);
    */
    /*
    cerr << "building csa of edges" << endl;
    //string edges_file = "@edges.iv";
    //store_to_file(e_iv, edges_file);
    construct_im(e_csa, e_iv, 1);
    */

    // to label the paths we'll need to compress and index our vectors
    util::bit_compress(s_iv);
    util::bit_compress(f_iv);
    util::bit_compress(t_iv);
    //util::bit_compress(e_iv);

    //construct_im(e_csa, e_iv, 8);

    util::assign(s_bv_rank, rank_support_v<1>(&s_bv));
    util::assign(s_bv_select, bit_vector::select_1_type(&s_bv));
    util::assign(f_bv_rank, rank_support_v<1>(&f_bv));
    util::assign(f_bv_select, bit_vector::select_1_type(&f_bv));
    util::assign(t_bv_rank, rank_support_v<1>(&t_bv));
    util::assign(t_bv_select, bit_vector::select_1_type(&t_bv));
    
    // compressed vectors of the above
    //vlc_vector<> s_civ(s_iv);
    util::assign(s_cbv, rrr_vector<>(s_bv));
    util::assign(s_cbv_rank, rrr_vector<>::rank_1_type(&s_cbv));
    util::assign(s_cbv_select, rrr_vector<>::select_1_type(&s_cbv));

#ifdef VERBOSE_DEBUG
    cerr << "storing paths" << endl;
#endif
    // paths
    //path_nodes[name].push_back(m.position().node_id());
    string path_names;
    size_t path_entities = 0; // count of nodes and edges
    for (auto& pathpair : path_nodes) {
        // add path name
        const string& path_name = pathpair.first;
        //cerr << path_name << endl;
        const vector<Traversal>& walk = pathpair.second;
        path_names += start_marker + path_name + end_marker;
        XGPath* path = new XGPath(path_name, walk, entity_count, *this, node_label);
        paths.push_back(path);
        path_entities += path->member_count;
    }

    // handle path names
    util::assign(pn_iv, int_vector<>(path_names.size()));
    util::assign(pn_bv, bit_vector(path_names.size()));
    // now record path name starts
    for (size_t i = 0; i < path_names.size(); ++i) {
        pn_iv[i] = path_names[i];
        if (path_names[i] == start_marker) {
            pn_bv[i] = 1; // register name start
        }
    }
    util::assign(pn_bv_rank, rank_support_v<1>(&pn_bv));
    util::assign(pn_bv_select, bit_vector::select_1_type(&pn_bv));
    
    //util::bit_compress(pn_iv);
    string path_name_file = "@pathnames.iv";
    store_to_file((const char*)path_names.c_str(), path_name_file);
    construct(pn_csa, path_name_file, 1);

    // entity -> paths
    util::assign(ep_iv, int_vector<>(path_entities+entity_count));
    util::assign(ep_bv, bit_vector(path_entities+entity_count));
    size_t ep_off = 0;
    for (size_t i = 0; i < entity_count; ++i) {
        ep_bv[ep_off] = 1;
        ep_iv[ep_off++] = 0; // null so we can detect entities with no path membership
        for (size_t j = 0; j < paths.size(); ++j) {
            if (paths[j]->members[i] == 1) {
                /*
                if (entity_is_node(i+1)) {
                    cerr << "node " << rank_to_id(entity_rank_as_node_rank(i+1)) << " is in path " << j+1 << endl;
                } else {
                    cerr << "edge " << " is in path " << j+1 << endl;
                }
                */
                ep_iv[ep_off++] = j+1;
            }
        }
    }

    util::bit_compress(ep_iv);
    //cerr << ep_off << " " << path_entities << " " << entity_count << endl;
    assert(ep_off == path_entities+entity_count);
    util::assign(ep_bv_rank, rank_support_v<1>(&ep_bv));
    util::assign(ep_bv_select, bit_vector::select_1_type(&ep_bv));

#ifdef DEBUG_CONSTRUCTION
    cerr << "|s_iv| = " << size_in_mega_bytes(s_iv) << endl;
    cerr << "|f_iv| = " << size_in_mega_bytes(f_iv) << endl;
    cerr << "|t_iv| = " << size_in_mega_bytes(t_iv) << endl;

    cerr << "|f_from_start_cbv| = " << size_in_mega_bytes(f_from_start_cbv) << endl;
    cerr << "|t_to_end_cbv| = " << size_in_mega_bytes(t_to_end_cbv) << endl;

    cerr << "|f_bv| = " << size_in_mega_bytes(f_bv) << endl;
    cerr << "|t_bv| = " << size_in_mega_bytes(t_bv) << endl;

    cerr << "|i_iv| = " << size_in_mega_bytes(i_iv) << endl;
    cerr << "|i_wt| = " << size_in_mega_bytes(i_wt) << endl;

    cerr << "|s_cbv| = " << size_in_mega_bytes(s_cbv) << endl;

    long double paths_mb_size = 0;
    cerr << "|pn_iv| = " << size_in_mega_bytes(pn_iv) << endl;
    paths_mb_size += size_in_mega_bytes(pn_iv);
    cerr << "|pn_csa| = " << size_in_mega_bytes(pn_csa) << endl;
    paths_mb_size += size_in_mega_bytes(pn_csa);
    cerr << "|pn_bv| = " << size_in_mega_bytes(pn_bv) << endl;
    paths_mb_size += size_in_mega_bytes(pn_bv);
    paths_mb_size += size_in_mega_bytes(pn_bv_rank);
    paths_mb_size += size_in_mega_bytes(pn_bv_select);
    paths_mb_size += size_in_mega_bytes(pi_iv);
    cerr << "|ep_iv| = " << size_in_mega_bytes(ep_iv) << endl;
    paths_mb_size += size_in_mega_bytes(ep_iv);
    cerr << "|ep_bv| = " << size_in_mega_bytes(ep_bv) << endl;
    paths_mb_size += size_in_mega_bytes(ep_bv);
    paths_mb_size += size_in_mega_bytes(ep_bv_rank);
    paths_mb_size += size_in_mega_bytes(ep_bv_select);
    cerr << "total paths size " << paths_mb_size << endl;
    // TODO you are missing the rest of the paths size in xg::paths
    // but this fragment should be factored into a function anyway
    
    cerr << "total size [MB] = " << (
        size_in_mega_bytes(s_iv)
        + size_in_mega_bytes(f_iv)
        + size_in_mega_bytes(t_iv)
        //+ size_in_mega_bytes(s_bv)
        + size_in_mega_bytes(f_bv)
        + size_in_mega_bytes(t_bv)
        + size_in_mega_bytes(i_iv)
        + size_in_mega_bytes(i_wt)
        + size_in_mega_bytes(s_cbv)
        + paths_mb_size
        ) << endl;

#endif

    if (print_graph) {
        cerr << "printing graph" << endl;
        cerr << s_iv << endl;
        for (int i = 0; i < s_iv.size(); ++i) {
            cerr << revdna3bit(s_iv[i]);
        } cerr << endl;
        cerr << s_bv << endl;
        cerr << i_iv << endl;
        cerr << f_iv << endl;
        cerr << f_bv << endl;
        cerr << t_iv << endl;
        cerr << t_bv << endl;
        cerr << "paths" << endl;
        for (auto& path : paths) {
            cerr << path->name << endl;
            cerr << path->members << endl;
            cerr << path->ids << endl;
            cerr << path->directions << endl;
            cerr << path->positions << endl;
            cerr << path->offsets << endl;
        }
        cerr << ep_bv << endl;
        cerr << ep_iv << endl;
    }

    if (validate_graph) {
        cerr << "validating graph sequence" << endl;
        int max_id = s_cbv_rank(s_cbv.size());
        for (auto& p : node_label) {
            int64_t id = p.first;
            const string& l = p.second;
            //size_t rank = node_rank[id];
            size_t rank = id_to_rank(id);
            //cerr << rank << endl;
            // find the node in the array
            //cerr << "id = " << id << " rank = " << s_cbv_select(rank) << endl;
            // this should be true given how we constructed things
            if (rank != s_cbv_rank(s_cbv_select(rank)+1)) {
                cerr << rank << " != " << s_cbv_rank(s_cbv_select(rank)+1) << " for node " << id << endl;
                assert(false);
            }
            // get the sequence from the s_iv
            string s = node_sequence(id);

            string ltmp, stmp;
            if (l.size() != s.size()) {
                cerr << l << " != " << endl << s << endl << " for node " << id << endl;
                assert(false);
            } else {
                int j = 0;
                for (auto c : l) {
                    if (dna3bit(c) != dna3bit(s[j++])) {
                        cerr << l << " != " << endl << s << endl << " for node " << id << endl;
                        assert(false);
                    }
                }
            }
        }
        node_label.clear();

        // -1 here seems weird
        // what?
        cerr << "validating forward edge table" << endl;
        for (size_t j = 0; j < f_iv.size()-1; ++j) {
            if (f_bv[j] == 1) continue;
            // from id == rank
            size_t fid = i_iv[f_bv_rank(j)-1];
            // to id == f_cbv[j]
            size_t tid = i_iv[f_iv[j]-1];
            bool from_start = f_from_start_bv[j];
            // get the to_end
            bool to_end = false;
            for (auto& side : from_to[Side(fid, from_start)]) {
                if (side.first == tid) {
                    to_end = side.second;
                }
            }
            if (from_to[Side(fid, from_start)].count(Side(tid, to_end)) == 0) {
                cerr << "could not find edge (f) "
                     << fid << (from_start ? "+" : "-")
                     << " -> "
                     << tid << (to_end ? "+" : "-")
                     << endl;
                assert(false);
            }
        }

        cerr << "validating reverse edge table" << endl;
        for (size_t j = 0; j < t_iv.size()-1; ++j) {
            //cerr << j << endl;
            if (t_bv[j] == 1) continue;
            // from id == rank
            size_t tid = i_iv[t_bv_rank(j)-1];
            // to id == f_cbv[j]
            size_t fid = i_iv[t_iv[j]-1];
            //cerr << tid << " " << fid << endl;

            bool to_end = t_to_end_bv[j];
            // get the to_end
            bool from_start = false;
            for (auto& side : to_from[Side(tid, to_end)]) {
                if (side.first == fid) {
                    from_start = side.second;
                }
            }
            if (to_from[Side(tid, to_end)].count(Side(fid, from_start)) == 0) {
                cerr << "could not find edge (t) "
                     << fid << (from_start ? "+" : "-")
                     << " -> "
                     << tid << (to_end ? "+" : "-")
                     << endl;
                assert(false);
            }
        }
    
        cerr << "validating paths" << endl;
        for (auto& pathpair : path_nodes) {
            const string& name = pathpair.first;
            const vector<Traversal>& path = pathpair.second;
            size_t prank = path_rank(name);
            //cerr << path_name(prank) << endl;
            assert(path_name(prank) == name);
            sd_vector<>& pe_bv = paths[prank-1]->members;
            int_vector<>& pp_iv = paths[prank-1]->positions;
            sd_vector<>& dir_bv = paths[prank-1]->directions;
            // check each entity in the nodes is present
            // and check node reported at the positions in it
            size_t pos = 0;
            size_t in_path = 0;
            for (auto& t : path) {
                int64_t id = t.id;
                bool rev = t.rev;
                assert(pe_bv[node_rank_as_entity(id)-1]);
                assert(dir_bv[in_path] == rev);
                Node n = node(id);
                //cerr << id << " in " << name << " at " << node_position_in_path(id, name) << " == " << pos << endl;
                assert(node_position_in_path(id, name) == pos);
                for (size_t k = 0; k < n.sequence().size(); ++k) {
                    //cerr << "id " << id << " ==? " << node_at_path_position(name, pos+k) << endl;
                    assert(id == node_at_path_position(name, pos+k));
                }
                pos += n.sequence().size();
                ++in_path;
            }
            //cerr << path_name << " rank = " << prank << endl;
            // check membership now for each entity in the path
        }

        cerr << "graph ok" << endl;
    }
}

Node XG::node(int64_t id) const {
    Node n;
    n.set_id(id);
    //cerr << omp_get_thread_num() << " looks for " << id << endl;
    n.set_sequence(node_sequence(id));
    return n;
}

string XG::node_sequence(int64_t id) const {
    size_t rank = id_to_rank(id);
    size_t start = s_cbv_select(rank);
    size_t end = rank == node_count ? s_cbv.size() : s_cbv_select(rank+1);
    string s; s.resize(end-start);
    for (size_t i = start; i < s_cbv.size() && i < end; ++i) {
        s[i-start] = revdna3bit(s_iv[i]);
    }
    return s;
}

size_t XG::id_to_rank(int64_t id) const {
    return r_iv[id-min_id];
}

int64_t XG::rank_to_id(size_t rank) const {
    //assert(rank > 0);
    return i_iv[rank-1];
}

vector<Edge> XG::edges_of(int64_t id) const {
    auto e1 = edges_to(id);
    auto e2 = edges_from(id);
    e1.reserve(e1.size() + distance(e2.begin(), e2.end()));
    e1.insert(e1.end(), e2.begin(), e2.end());
    // now get rid of duplicates
    vector<Edge> e3;
    set<string> seen;
    for (auto& edge : e1) {
        string s; edge.SerializeToString(&s);
        if (!seen.count(s)) {
            e3.push_back(edge);
            seen.insert(s);
        }
    }
    return e3;
}

vector<Edge> XG::edges_to(int64_t id) const {
    vector<Edge> edges;
    size_t rank = id_to_rank(id);
    size_t t_start = t_bv_select(rank)+1;
    size_t t_end = rank == node_count ? t_bv.size() : t_bv_select(rank+1);
    for (size_t i = t_start; i < t_end; ++i) {
        Edge edge;
        edge.set_to(id);
        edge.set_from(rank_to_id(t_iv[i]));
        edge.set_from_start(t_from_start_cbv[i]);
        edge.set_to_end(t_to_end_cbv[i]);
        edges.push_back(edge);
    }
    return edges;
}

vector<Edge> XG::edges_from(int64_t id) const {
    vector<Edge> edges;
    size_t rank = id_to_rank(id);
    size_t f_start = f_bv_select(rank)+1;
    size_t f_end = rank == node_count ? f_bv.size() : f_bv_select(rank+1);
    for (size_t i = f_start; i < f_end; ++i) {
        Edge edge;
        edge.set_from(id);
        edge.set_to(rank_to_id(f_iv[i]));
        edge.set_from_start(f_from_start_cbv[i]);
        edge.set_to_end(f_to_end_cbv[i]);
        edges.push_back(edge);
    }
    return edges;
}

vector<Edge> XG::edges_on_start(int64_t id) const {
    vector<Edge> edges;
    for (auto& edge : edges_of(id)) {
        if (edge.to() == id || edge.from_start()) {
            edges.push_back(edge);
        }
    }
    return edges;
}

vector<Edge> XG::edges_on_end(int64_t id) const {
    vector<Edge> edges;
    for (auto& edge : edges_of(id)) {
        if (edge.from() == id || edge.to_end()) {
            edges.push_back(edge);
        }
    }
    return edges;
}

size_t XG::max_node_rank(void) const {
    return s_cbv_rank(s_cbv.size());
}

size_t XG::max_path_rank(void) const {
    //cerr << pn_bv << endl;
    //cerr << "..." << pn_bv_rank(pn_bv.size()) << endl;
    return pn_bv_rank(pn_bv.size());
}

size_t XG::node_rank_as_entity(int64_t id) const {
    //cerr << id_to_rank(id) << endl;
    return f_bv_select(id_to_rank(id))+1;
}

bool XG::entity_is_node(size_t rank) const {
    return 1 == f_bv[rank-1];
}

size_t XG::entity_rank_as_node_rank(size_t rank) const {
    return !entity_is_node(rank) ? 0 : f_iv[rank-1];
}

// snoop through the forward table to check if the edge exists
bool XG::has_edge(int64_t id1, int64_t id2) const {
    size_t rank1 = id_to_rank(id1);
    size_t rank2 = id_to_rank(id2);
    size_t f_start = f_bv_select(rank1);
    size_t f_end = rank1 == node_count ? f_bv.size() : f_bv_select(rank1+1);
    for (size_t i = f_start; i < f_end; ++i) {
        if (rank2 == f_iv[i]) {
            return true;
        }
    }
    return false;
}

size_t XG::edge_rank_as_entity(int64_t id1, int64_t id2) const {
    size_t rank1 = id_to_rank(id1);
    size_t rank2 = id_to_rank(id2);
    //cerr << "Finding rank for " << id1 << "(" << rank1 << ") " << " -> " << id2 << "(" << rank2 << ")"<< endl;
    size_t f_start = f_bv_select(rank1);
    size_t f_end = rank1 == node_count ? f_bv.size() : f_bv_select(rank1+1);
    //cerr << f_start << " to " << f_end << endl;
    for (size_t i = f_start; i < f_end; ++i) {
        //cerr << f_iv[i] << endl;
        if (rank2 == f_iv[i]) {
            //cerr << i << endl;
            return i+1;
        }
    }
    cerr << "edge does not exist: " << id1 << " -> " << id2 << endl;
    assert(false);
}

size_t XG::path_rank(const string& name) const {
    // find the name in the csa
    string query = start_marker + name + end_marker;
    auto occs = locate(pn_csa, query);
    if (occs.size() > 1) {
        cerr << "multiple hits for " << query << endl;
        assert(false);
    }
    //cerr << "path named " << name << " is at " << occs[0] << endl;
    return pn_bv_rank(occs[0])+1; // step past '#'
}

string XG::path_name(size_t rank) const {
    //cerr << "path rank " << rank << endl;
    size_t start = pn_bv_select(rank)+1; // step past '#'
    size_t end = rank == path_count ? pn_iv.size() : pn_bv_select(rank+1);
    end -= 1;  // step before '$'
    string name; name.resize(end-start);
    for (size_t i = start; i < end; ++i) {
        name[i-start] = pn_iv[i];
    }
    return name;
}

bool XG::path_contains_entity(const string& name, size_t rank) const {
    return 1 == paths[path_rank(name)-1]->members[rank-1];
}

bool XG::path_contains_node(const string& name, int64_t id) const {
    return path_contains_entity(name, node_rank_as_entity(id));
}

bool XG::path_contains_edge(const string& name, int64_t id1, int64_t id2) const {
    return path_contains_entity(name, edge_rank_as_entity(id1, id2));
}

vector<size_t> XG::paths_of_entity(size_t rank) const {
    size_t off = ep_bv_select(rank);
    assert(ep_bv[off++]);
    vector<size_t> path_ranks;
    while (off < ep_bv.size() && ep_bv[off] == 0) {
        path_ranks.push_back(ep_iv[off++]);
    }
    return path_ranks;
}

vector<size_t> XG::paths_of_node(int64_t id) const {
    return paths_of_entity(node_rank_as_entity(id));
}

vector<size_t> XG::paths_of_edge(int64_t id1, int64_t id2) const {
    return paths_of_entity(edge_rank_as_entity(id1, id2));
}

map<string, Mapping> XG::node_mappings(int64_t id) const {
    map<string, Mapping> mappings;
    for (auto i : paths_of_entity(node_rank_as_entity(id))) {
        // get the path name
        string name = path_name(i);
        mappings[name] = new_mapping(name, id);
    }
    return mappings;
}

void XG::neighborhood(int64_t id, size_t steps, Graph& g) const {
    *g.add_node() = node(id);
    expand_context(g, steps);
}

void XG::expand_context(Graph& g, size_t steps) const {
    map<int64_t, Node*> nodes;
    map<pair<Side, Side>, Edge*> edges;
    set<int64_t> to_visit;
    // start with the nodes in the graph
    for (size_t i = 0; i < g.node_size(); ++i) {
        to_visit.insert(g.node(i).id());
        // handles the single-node case: we should still get the paths
        Node* np = g.mutable_node(i);
        nodes[np->id()] = np;
    }
    for (size_t i = 0; i < g.edge_size(); ++i) {
        auto& edge = g.edge(i);
        to_visit.insert(edge.from());
        to_visit.insert(edge.to());
        edges[make_pair(Side(edge.from(), edge.from_start()),
                        Side(edge.to(), edge.to_end()))] = g.mutable_edge(i);
    }
    // and expand
    for (size_t i = 0; i < steps; ++i) {
        set<int64_t> to_visit_next;
        for (auto id : to_visit) {
            // build out the graph
            // if we have nodes we haven't seeen
            if (nodes.find(id) == nodes.end()) {
                Node* np = g.add_node();
                nodes[id] = np;
                *np = node(id);
            }
            for (auto& edge : edges_of(id)) {
                auto sides = make_pair(Side(edge.from(), edge.from_start()),
                                       Side(edge.to(), edge.to_end()));
                if (edges.find(sides) == edges.end()) {
                    Edge* ep = g.add_edge(); *ep = edge;
                    edges[sides] = ep;
                }
                if (edge.from() == id) {
                    to_visit_next.insert(edge.to());
                } else {
                    to_visit_next.insert(edge.from());
                }
            }
        }
        to_visit = to_visit_next;
    }
    // then add connected nodes so we don't have orphan edges
    to_visit.clear();
    for (auto& e : edges) {
        auto& edge = e.second;
        // get missing nodes
        int64_t f = edge->from();
        if (nodes.find(f) == nodes.end()) {
            Node* np = g.add_node();
            nodes[f] = np;
            *np = node(f);
            to_visit.insert(f);
        }
        int64_t t = edge->to();
        if (nodes.find(t) == nodes.end()) {
            Node* np = g.add_node();
            nodes[t] = np;
            *np = node(t);
            to_visit.insert(t);
        }
    }
    
    // add in the edges between the newly added nodes. All their other edges
    // that wou;dn't be orphaned have been accounted for.
    for (auto& id : to_visit) {
        for (auto& edge : edges_of(id)) {
            if(!to_visit.count(edge.from()) || !to_visit.count(edge.to())) {
                // this edge would be orphaned or should already be there.
                continue;
            }
        
            auto sides = make_pair(Side(edge.from(), edge.from_start()),
                                   Side(edge.to(), edge.to_end()));
            if (edges.find(sides) == edges.end()) {
                // Add the edge if we don't have it
                Edge* ep = g.add_edge(); *ep = edge;
                edges[sides] = ep;
            }
        }
    }
    
    add_paths_to_graph(nodes, g);
}

    
// if the graph ids partially ordered, this works no prob
// otherwise... owch
// the paths become disordered due to traversal of the node ids in order
void XG::add_paths_to_graph(map<int64_t, Node*>& nodes, Graph& g) const {
    // pick up current paths
    map<string, Path*> paths;
    for (size_t i = 0; i < g.path_size(); ++i) {
        paths[g.path(i).name()] = g.mutable_path(i);
    }
    // store in graph
    for (auto& n : nodes) {
        auto& node = n.second;
        for (auto& m : node_mappings(n.first)) {
            if (paths.find(m.first) == paths.end()) {
                Path* p = g.add_path();
                paths[m.first] = p;
                p->set_name(m.first);
            }
            Path* path = paths[m.first];
            *path->add_mapping() = m.second;
        }
    }
}

void XG::get_id_range(int64_t id1, int64_t id2, Graph& g) const {
    id1 = max(min_id, id1);
    id2 = min(max_id, id2);
    for (auto i = id1; i <= id2; ++i) {
        *g.add_node() = node(i);
    }
}

/*
void XG::get_connected_nodes(Graph& g) {
}
*/

size_t XG::path_length(const string& name) const {
    return paths[path_rank(name)-1]->offsets.size();
}

// TODO, include paths
void XG::get_path_range(string& name, int64_t start, int64_t stop, Graph& g) const {
    // what is the node at the start, and at the end
    auto& path = *paths[path_rank(name)-1];
    size_t plen = path.offsets.size();
    if (start > plen) return; // no overlap with path
    size_t pr1 = path.offsets_rank(start+1)-1;
    // careful not to exceed the path length
    if (stop >= plen) stop = plen-1;
    size_t pr2 = path.offsets_rank(stop+1)-1;
    set<int64_t> nodes;
    set<pair<Side, Side> > edges;
    auto& pi_wt = path.ids;
    for (size_t i = pr1; i <= pr2; ++i) {
        int64_t id = rank_to_id(pi_wt[i]);
        nodes.insert(id);
        for (auto& e : edges_from(id)) {
            edges.insert(make_pair(Side(e.from(), e.from_start()), Side(e.to(), e.to_end())));
        }
        for (auto& e : edges_to(id)) {
            edges.insert(make_pair(Side(e.from(), e.from_start()), Side(e.to(), e.to_end())));
        }
    }
    for (auto& n : nodes) {
        *g.add_node() = node(n);
    }
    map<string, Path*> local_paths;
    for (auto& n : nodes) {
        for (auto& m : node_mappings(n)) {
            if (local_paths.find(m.first) == local_paths.end()) {
                Path* p = g.add_path();
                local_paths[m.first] = p;
                p->set_name(m.first);
            }
            Path* new_path = local_paths[m.first];
            // TODO output mapping direction
            //if () { m.second.is_reverse(true); }
            *new_path->add_mapping() = m.second;
        }
    }
    for (auto& e : edges) {
        Edge edge;
        edge.set_from(e.first.first);
        edge.set_from_start(e.first.second);
        edge.set_to(e.second.first);
        edge.set_to_end(e.second.second);
        *g.add_edge() = edge;
    }
}

size_t XG::node_occs_in_path(int64_t id, const string& name) const {
    size_t p = path_rank(name)-1;
    auto& pi_wt = paths[path_rank(name)-1]->ids;
    return pi_wt.rank(pi_wt.size(), id_to_rank(id));
}

size_t XG::node_position_in_path(int64_t id, const string& name) const {
    if (node_occs_in_path(id, name) > 1) {
        // not handled yet...
        cerr << "warning: path " << name << " contains a loop" << endl;
    }
    auto& path = *paths[path_rank(name)-1];
    return path.positions[path.ids.select(1, id_to_rank(id))];
}

int64_t XG::node_at_path_position(const string& name, size_t pos) const {
    auto& path = *paths[path_rank(name)-1];
    return rank_to_id(path.ids[path.offsets_rank(pos+1)-1]);
}

Mapping new_mapping(const string& name, int64_t id) {
    Mapping m;
    m.mutable_position()->set_node_id(id);
    return m;
}

void parse_region(const string& target, string& name, int64_t& start, int64_t& end) {
    start = -1;
    end = -1;
    size_t foundFirstColon = target.find(":");
    // we only have a single string, use the whole sequence as the target
    if (foundFirstColon == string::npos) {
        name = target;
    } else {
        name = target.substr(0, foundFirstColon);
	    size_t foundRangeDash = target.find("-", foundFirstColon);
        if (foundRangeDash == string::npos) {
            start = atoi(target.substr(foundFirstColon + 1).c_str());
            end = start;
        } else {
            start = atoi(target.substr(foundFirstColon + 1, foundRangeDash - foundRangeDash - 1).c_str());
            end = atoi(target.substr(foundRangeDash + 1).c_str());
        }
    }
}

void to_text(ostream& out, Graph& graph) {
    out << "H" << "\t" << "HVN:Z:1.0" << endl;
    for (size_t i = 0; i < graph.node_size(); ++i) {
        auto& node = graph.node(i);
        out << "S" << "\t" << node.id() << "\t" << node.sequence() << endl;
    }
    for (size_t i = 0; i < graph.path_size(); ++i) {
        auto& path = graph.path(i);
        for (size_t j = 0; j < path.mapping_size(); ++j) {
            auto& mapping = path.mapping(i);
            string orientation = mapping.is_reverse() ? "-" : "+";
            out << "P" << "\t" << mapping.position().node_id()
                << "\t" << path.name() << "\t"
                << orientation << endl;
        }
    }
    for (int i = 0; i < graph.edge_size(); ++i) {
        auto& edge = graph.edge(i);
        out << "L" << "\t" << edge.from() << "\t"
            << (edge.from_start() ? "-" : "+") << "\t"
            << edge.to() << "\t"
            << (edge.to_end() ? "-" : "+") << endl;
    }
}

}
