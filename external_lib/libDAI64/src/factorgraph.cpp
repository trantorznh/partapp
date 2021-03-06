/*  Copyright (C) 2006-2008  Joris Mooij  [joris dot mooij at tuebingen dot mpg dot de]
    Radboud University Nijmegen, The Netherlands /
    Max Planck Institute for Biological Cybernetics, Germany

    This file is part of libDAI.

    libDAI is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    libDAI is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libDAI; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <fstream>
#include <string>
#include <algorithm>
#include <functional>
#include <dai/factorgraph.h>
#include <dai/util.h>
#include <dai/exceptions.h>


namespace dai {


using namespace std;


FactorGraph::FactorGraph( const std::vector<Factor> &P ) : G(), _backup() {
    // add factors, obtain variables
    set<Var> varset;
    _factors.reserve( P.size() );
    size_t nrEdges = 0;
    for( vector<Factor>::const_iterator p2 = P.begin(); p2 != P.end(); p2++ ) {
        _factors.push_back( *p2 );
        copy( p2->vars().begin(), p2->vars().end(), inserter( varset, varset.begin() ) );
        nrEdges += p2->vars().size();
    }

    // add vars
    _vars.reserve( varset.size() );
    for( set<Var>::const_iterator p1 = varset.begin(); p1 != varset.end(); p1++ )
        _vars.push_back( *p1 );

    // create graph structure
    constructGraph( nrEdges );
}


void FactorGraph::constructGraph( size_t nrEdges ) {
    // create a mapping for indices
    hash_map<size_t, size_t> hashmap;
    
    for( size_t i = 0; i < vars().size(); i++ )
        hashmap[var(i).label()] = i;
    
    // create edge list
    vector<Edge> edges;
    edges.reserve( nrEdges );
    for( size_t i2 = 0; i2 < nrFactors(); i2++ ) {
        const VarSet& ns = factor(i2).vars();
        for( VarSet::const_iterator q = ns.begin(); q != ns.end(); q++ )
            edges.push_back( Edge(hashmap[q->label()], i2) );
    }

    // create bipartite graph
    G.construct( nrVars(), nrFactors(), edges.begin(), edges.end() );
}


/// Writes a FactorGraph to an output stream
ostream& operator << (ostream& os, const FactorGraph& fg) {
    os << fg.nrFactors() << endl;

    for( size_t I = 0; I < fg.nrFactors(); I++ ) {
        os << endl;
        os << fg.factor(I).vars().size() << endl;
        for( VarSet::const_iterator i = fg.factor(I).vars().begin(); i != fg.factor(I).vars().end(); i++ )
            os << i->label() << " ";
        os << endl;
        for( VarSet::const_iterator i = fg.factor(I).vars().begin(); i != fg.factor(I).vars().end(); i++ )
            os << i->states() << " ";
        os << endl;
        size_t nr_nonzeros = 0;
        for( size_t k = 0; k < fg.factor(I).states(); k++ )
            if( fg.factor(I)[k] != 0.0 )
                nr_nonzeros++;
        os << nr_nonzeros << endl;
        for( size_t k = 0; k < fg.factor(I).states(); k++ )
            if( fg.factor(I)[k] != 0.0 ) {
                char buf[20];
                sprintf(buf,"%18.14g", fg.factor(I)[k]);
                os << k << " " << buf << endl;
            }
    }

    return(os);
}


/// Reads a FactorGraph from an input stream
istream& operator >> (istream& is, FactorGraph& fg) {
    long verbose = 0;

    try {
        vector<Factor> facs;
        size_t nr_Factors;
        string line;
        
        while( (is.peek()) == '#' )
            getline(is,line);
        is >> nr_Factors;
        if( is.fail() )
            DAI_THROW(INVALID_FACTORGRAPH_FILE);
        if( verbose >= 2 )
            cout << "Reading " << nr_Factors << " factors..." << endl;

        getline (is,line);
        if( is.fail() )
            DAI_THROW(INVALID_FACTORGRAPH_FILE);

        map<long,size_t> vardims;
        for( size_t I = 0; I < nr_Factors; I++ ) {
            if( verbose >= 3 )
                cout << "Reading factor " << I << "..." << endl;
            size_t nr_members;
            while( (is.peek()) == '#' )
                getline(is,line);
            is >> nr_members;
            if( verbose >= 3 )
                cout << "  nr_members: " << nr_members << endl;

            vector<long> labels;
            for( size_t mi = 0; mi < nr_members; mi++ ) {
                long mi_label;
                while( (is.peek()) == '#' )
                    getline(is,line);
                is >> mi_label;
                labels.push_back(mi_label);
            }
            if( verbose >= 3 )
                cout << "  labels: " << labels << endl;

            vector<size_t> dims;
            for( size_t mi = 0; mi < nr_members; mi++ ) {
                size_t mi_dim;
                while( (is.peek()) == '#' )
                    getline(is,line);
                is >> mi_dim;
                dims.push_back(mi_dim);
            }
            if( verbose >= 3 )
                cout << "  dimensions: " << dims << endl;

            // add the Factor
            VarSet I_vars;
            for( size_t mi = 0; mi < nr_members; mi++ ) {
                map<long,size_t>::iterator vdi = vardims.find( labels[mi] );
                if( vdi != vardims.end() ) {
                    // check whether dimensions are consistent
                    if( vdi->second != dims[mi] )
                        DAI_THROW(INVALID_FACTORGRAPH_FILE);
                } else
                    vardims[labels[mi]] = dims[mi];
                I_vars |= Var(labels[mi], dims[mi]);
            }
            facs.push_back( Factor( I_vars, 0.0 ) );
            
            // calculate permutation sigma (internally, members are sorted)
            vector<size_t> sigma(nr_members,0);
            VarSet::iterator j = I_vars.begin();
            for( size_t mi = 0; mi < nr_members; mi++,j++ ) {
                long search_for = j->label();
                vector<long>::iterator j_loc = find(labels.begin(),labels.end(),search_for);
                sigma[mi] = j_loc - labels.begin();
            }
            if( verbose >= 3 )
                cout << "  sigma: " << sigma << endl;

            // calculate multindices
            Permute permindex( dims, sigma );
            
            // read values
            size_t nr_nonzeros;
            while( (is.peek()) == '#' )
                getline(is,line);
            is >> nr_nonzeros;
            if( verbose >= 3 ) 
                cout << "  nonzeroes: " << nr_nonzeros << endl;
            for( size_t k = 0; k < nr_nonzeros; k++ ) {
                size_t li;
                double val;
                while( (is.peek()) == '#' )
                    getline(is,line);
                is >> li;
                while( (is.peek()) == '#' )
                    getline(is,line);
                is >> val;

                // store value, but permute indices first according
                // to internal representation
                facs.back()[permindex.convert_linear_index( li  )] = val;
            }
        }

        if( verbose >= 3 )
            cout << "factors:" << facs << endl;

        fg = FactorGraph(facs);
    } catch (char *e) {
        cout << e << endl;
    }

    return is;
}


VarSet FactorGraph::delta( unsigned i ) const {
    return( Delta(i) / var(i) );
}


VarSet FactorGraph::Delta( unsigned i ) const {
    // calculate Markov Blanket
    VarSet Del;
    foreach( const Neighbor &I, nbV(i) ) // for all neighboring factors I of i
        foreach( const Neighbor &j, nbF(I) ) // for all neighboring variables j of I
            Del |= var(j);

    return Del;
}


VarSet FactorGraph::Delta( const VarSet &ns ) const {
    VarSet result;
    for( VarSet::const_iterator n = ns.begin(); n != ns.end(); n++ ) 
        result |= Delta(findVar(*n));
    return result;
}


void FactorGraph::makeCavity( unsigned i, bool backup ) {
    // fills all Factors that include var(i) with ones
    map<size_t,Factor> newFacs;
    foreach( const Neighbor &I, nbV(i) ) // for all neighboring factors I of i
        newFacs[I] = Factor(factor(I).vars(), 1.0);
    setFactors( newFacs, backup );
}


void FactorGraph::ReadFromFile( const char *filename ) {
    ifstream infile;
    infile.open( filename );
    if( infile.is_open() ) {
        infile >> *this;
        infile.close();
    } else
        DAI_THROW(CANNOT_READ_FILE);
}


void FactorGraph::WriteToFile( const char *filename ) const {
    ofstream outfile;
    outfile.open( filename );
    if( outfile.is_open() ) {
        outfile << *this;
        outfile.close();
    } else
        DAI_THROW(CANNOT_WRITE_FILE);
}


void FactorGraph::printDot( std::ostream &os ) const {
    os << "graph G {" << endl;
    os << "node[shape=circle,width=0.4,fixedsize=true];" << endl;
    for( size_t i = 0; i < nrVars(); i++ )
        os << "\tv" << var(i).label() << ";" << endl;
    os << "node[shape=box,width=0.3,height=0.3,fixedsize=true];" << endl;
    for( size_t I = 0; I < nrFactors(); I++ )
        os << "\tf" << I << ";" << endl;
    for( size_t i = 0; i < nrVars(); i++ )
        foreach( const Neighbor &I, nbV(i) )  // for all neighboring factors I of i
            os << "\tv" << var(i).label() << " -- f" << I << ";" << endl;
    os << "}" << endl;
}


vector<VarSet> FactorGraph::Cliques() const {
    vector<VarSet> result;
    
    for( size_t I = 0; I < nrFactors(); I++ ) {
        bool maximal = true;
        for( size_t J = 0; (J < nrFactors()) && maximal; J++ )
            if( (factor(J).vars() >> factor(I).vars()) && (factor(J).vars() != factor(I).vars()) )
                maximal = false;
        
        if( maximal )
            result.push_back( factor(I).vars() );
    }

    return result;
}


void FactorGraph::clamp( const Var & n, size_t i, bool backup ) {
    assert( i <= n.states() );

    // Multiply each factor that contains the variable with a delta function

    Factor delta_n_i(n,0.0);
    delta_n_i[i] = 1.0;

    map<size_t, Factor> newFacs;
    // For all factors that contain n
    for( size_t I = 0; I < nrFactors(); I++ ) 
        if( factor(I).vars().contains( n ) )
            // Multiply it with a delta function
            newFacs[I] = factor(I) * delta_n_i;
    setFactors( newFacs, backup );

    return;
}


void FactorGraph::backupFactor( size_t I ) {
    map<size_t,Factor>::iterator it = _backup.find( I );
    if( it != _backup.end() )
        DAI_THROW( MULTIPLE_UNDO );
    _backup[I] = factor(I);
}


void FactorGraph::restoreFactor( size_t I ) {
    map<size_t,Factor>::iterator it = _backup.find( I );
    if( it != _backup.end() ) {
        setFactor(I, it->second);
        _backup.erase(it);
    }
}


void FactorGraph::backupFactors( const VarSet &ns ) {
    for( size_t I = 0; I < nrFactors(); I++ )
        if( factor(I).vars().intersects( ns ) )
            backupFactor( I );
}


void FactorGraph::restoreFactors( const VarSet &ns ) {
    map<size_t,Factor> facs;
    for( map<size_t,Factor>::iterator uI = _backup.begin(); uI != _backup.end(); ) {
        if( factor(uI->first).vars().intersects( ns ) ) {
            facs.insert( *uI );
            _backup.erase(uI++);
        } else
            uI++;
    }
    setFactors( facs );
}


void FactorGraph::restoreFactors() {
    setFactors( _backup );
    _backup.clear();
}

void FactorGraph::backupFactors( const std::set<size_t> & facs ) {
    for( std::set<size_t>::const_iterator fac = facs.begin(); fac != facs.end(); fac++ )
        backupFactor( *fac );
}


bool FactorGraph::isPairwise() const {
    bool pairwise = true;
    for( size_t I = 0; I < nrFactors() && pairwise; I++ )
        if( factor(I).vars().size() > 2 )
            pairwise = false;
    return pairwise;
}


bool FactorGraph::isBinary() const {
    bool binary = true;
    for( size_t i = 0; i < nrVars() && binary; i++ )
        if( var(i).states() > 2 )
            binary = false;
    return binary;
}


FactorGraph FactorGraph::clamped( const Var & v_i, size_t state ) const {
    Real zeroth_order = 1.0;
    vector<Factor> clamped_facs;
    for( size_t I = 0; I < nrFactors(); I++ ) {
        VarSet v_I = factor(I).vars();
        Factor new_factor;
        if( v_I.intersects( v_i ) )
            new_factor = factor(I).slice( v_i, state );
        else
            new_factor = factor(I);

        if( new_factor.vars().size() != 0 ) {
            size_t J = 0;
            // if it can be merged with a previous one, do that
            for( J = 0; J < clamped_facs.size(); J++ )
                if( clamped_facs[J].vars() == new_factor.vars() ) {
                    clamped_facs[J] *= new_factor;
                    break;
                }
            // otherwise, push it back
            if( J == clamped_facs.size() || clamped_facs.size() == 0 )
                clamped_facs.push_back( new_factor );
        } else
            zeroth_order *= new_factor[0];
    }
    *(clamped_facs.begin()) *= zeroth_order;
    return FactorGraph( clamped_facs );
}


FactorGraph FactorGraph::maximalFactors() const {
    vector<size_t> maxfac( nrFactors() );
    map<size_t,size_t> newindex;
    size_t nrmax = 0;
    for( size_t I = 0; I < nrFactors(); I++ ) {
        maxfac[I] = I;
        VarSet maxfacvars = factor(maxfac[I]).vars();
        for( size_t J = 0; J < nrFactors(); J++ ) {
            VarSet Jvars = factor(J).vars();
            if( Jvars >> maxfacvars && (Jvars != maxfacvars) ) {
                maxfac[I] = J;
                maxfacvars = factor(maxfac[I]).vars();
            }
        }
        if( maxfac[I] == I )
            newindex[I] = nrmax++;
    }

    vector<Factor> facs( nrmax );
    for( size_t I = 0; I < nrFactors(); I++ )
        facs[newindex[maxfac[I]]] *= factor(I);

    return FactorGraph( facs.begin(), facs.end(), vars().begin(), vars().end(), facs.size(), nrVars() );
}


} // end of namespace dai
