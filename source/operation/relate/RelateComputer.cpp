#include "opRelate.h"
#include "stdio.h"
#include "util.h"

const LineIntersector* RelateComputer::li=new RobustLineIntersector();
const PointLocator* RelateComputer::ptLocator=new PointLocator();

RelateComputer::RelateComputer() {
	nodes=new NodeMap(new RelateNodeFactory());
	im=NULL;
	arg=NULL;
}

RelateComputer::RelateComputer(vector<GeometryGraph*> *newArg) {
	nodes=new NodeMap(new RelateNodeFactory());
	im=NULL;
	arg=newArg;
}

/**
* @return the intersection point, or <code>null</code> if none was found
*/
Coordinate RelateComputer::getInvalidPoint() {
	return invalidPoint;
}

bool RelateComputer::isNodeConsistentArea() {
	SegmentIntersector *intersector=arg->at(0)->computeSelfNodes((LineIntersector*)li);
	if (intersector->hasProperIntersection()) {
		invalidPoint.setCoordinate(intersector->getProperIntersectionPoint());
		return false;
	}
	// compute intersections between edges
	computeIntersectionNodes(0);
	/**
	* Copy the labelling for the nodes in the parent Geometry.  These override
	* any labels determined by intersections.
	*/
	copyNodesAndLabels(0);
	/**
	* Build EdgeEnds for all intersections.
	*/
	EdgeEndBuilder eeBuilder;
	vector<EdgeEnd*> *ee0=eeBuilder.computeEdgeEnds(arg->at(0)->getEdges());
	insertEdgeEnds(ee0);
	//Debug.println("==== NodeList ===");
	//Debug.print(nodes);
	return isNodeEdgeAreaLabelsConsistent();
}

/**
* Checks for two duplicate rings in an area.
* Duplicate rings are rings that are topologically equal
* (that is, which have the same sequence of points up to point order).
* If the area is topologically consistent (determined by calling the
* <code>isNodeConsistentArea</code>,
* duplicate rings can be found by checking for EdgeBundles which contain
* more than one EdgeEnd.
* (This is because topologically consistent areas cannot have two rings sharing
* the same line segment, unless the rings are equal).
* The start point of one of the equal rings will be placed in
* invalidPoint.
*
* @return true if this area Geometry is topologically consistent but has two duplicate rings
*/
bool RelateComputer::hasDuplicateRings() {
	map<Coordinate,Node*,CoordLT> nMap(nodes->nodeMap);
	map<Coordinate,Node*,CoordLT>::iterator nodeIt;
	for(nodeIt=nMap.begin();nodeIt!=nMap.end();nodeIt++) {
		RelateNode *node=(RelateNode*) nodeIt->second;
		vector<EdgeEnd*> *edges=node->getEdges()->getEdges();
		for(vector<EdgeEnd*>::iterator i=edges->begin();i<edges->end();i++) {
			EdgeEndBundle *eeb=(EdgeEndBundle*) *i;
			if (eeb->getEdgeEnds().size()>1) {
				invalidPoint.setCoordinate(eeb->getEdge()->getCoordinate(0));
				return true;
			}
		}
	}
	return false;
}

IntersectionMatrix RelateComputer::computeIM() {
	IntersectionMatrix *im=new IntersectionMatrix();
	// since Geometries are finite and embedded in a 2-D space, the EE element must always be 2
	im->set(Location::EXTERIOR,Location::EXTERIOR,2);
	// if the Geometries don't overlap there is nothing to do
	if (! arg->at(0)->getGeometry()->getEnvelopeInternal().overlaps(
							arg->at(1)->getGeometry()->getEnvelopeInternal())) {
		computeDisjointIM(im);
		return IntersectionMatrix(*im);
	}
	arg->at(0)->computeSelfNodes((LineIntersector*)li);
	arg->at(1)->computeSelfNodes((LineIntersector*)li);
	// compute intersections between edges of the two input geometries
	SegmentIntersector *intersector=arg->at(0)->computeEdgeIntersections(arg->at(1),(LineIntersector*)li,false);
	computeIntersectionNodes(0);
	computeIntersectionNodes(1);
	/**
	* Copy the labelling for the nodes in the parent Geometries.  These override
	* any labels determined by intersections between the geometries.
	*/
	copyNodesAndLabels(0);
	copyNodesAndLabels(1);
	// complete the labelling for any nodes which only have a label for a single geometry
	//Debug.addWatch(nodes.find(new Coordinate(110, 200)));
	//Debug.printWatch();
	labelIsolatedNodes();
	//Debug.printWatch();
	// If a proper intersection was found, we can set a lower bound on the IM.
	computeProperIntersectionIM(intersector,im);
	/**
	* Now process improper intersections
	* (eg where one or other of the geometrys has a vertex at the intersection point)
	* We need to compute the edge graph at all nodes to determine the IM.
	*/
	// build EdgeEnds for all intersections
	EdgeEndBuilder eeBuilder;
	vector<EdgeEnd*> *ee0=eeBuilder.computeEdgeEnds(arg->at(0)->getEdges());
	insertEdgeEnds(ee0);
	vector<EdgeEnd*> *ee1=eeBuilder.computeEdgeEnds(arg->at(1)->getEdges());
	insertEdgeEnds(ee1);
	//Debug.println("==== NodeList ===");
	//Debug.print(nodes);
	labelNodeEdges();
	/**
	* Compute the labeling for isolated components
	* <br>
	* Isolated components are components that do not touch any other components in the graph.
	* They can be identified by the fact that they will
	* contain labels containing ONLY a single element, the one for their parent geometry.
	* We only need to check components contained in the input graphs, since
	* isolated components will not have been replaced by new components formed by intersections.
	*/
	//debugPrintln("Graph A isolated edges - ");
	labelIsolatedEdges(0,1);
	//debugPrintln("Graph B isolated edges - ");
	labelIsolatedEdges(1,0);
	// update the IM from all components
	updateIM(im);
	return IntersectionMatrix(*im);
}

void RelateComputer::insertEdgeEnds(vector<EdgeEnd*> *ee) {
	for(vector<EdgeEnd*>::iterator i=ee->begin();i<ee->end();i++) {
		EdgeEnd *e=*i;
		nodes->add(e);
	}
}

void RelateComputer::computeProperIntersectionIM(SegmentIntersector *intersector,IntersectionMatrix *im) {
	// If a proper intersection is found, we can set a lower bound on the IM.
	int dimA=arg->at(0)->getGeometry()->getDimension();
	int dimB=arg->at(1)->getGeometry()->getDimension();
	bool hasProper=intersector->hasProperIntersection();
	bool hasProperInterior=intersector->hasProperInteriorIntersection();
	// For Geometrys of dim 0 there can never be proper intersections.
	/**
	* If edge segments of Areas properly intersect, the areas must properly overlap.
	*/
	if (dimA==2 && dimB==2) {
		if (hasProper) im->setAtLeast("212101212");
	}
	/**
	* If an Line segment properly intersects an edge segment of an Area,
	* it follows that the Interior of the Line intersects the Boundary of the Area.
	* If the intersection is a proper <i>interior</i> intersection, then
	* there is an Interior-Interior intersection too.
	* Note that it does not follow that the Interior of the Line intersects the Exterior
	* of the Area, since there may be another Area component which contains the rest of the Line.
	*/
	 else if (dimA==2 && dimB==1) {
		if (hasProper) im->setAtLeast("FFF0FFFF2");
		if (hasProperInterior) im->setAtLeast("1FFFFF1FF");
	} else if (dimA==1 && dimB==2) {
		if (hasProper) im->setAtLeast("F0FFFFFF2");
		if (hasProperInterior) im->setAtLeast("1F1FFFFFF");
	}
	/* If edges of LineStrings properly intersect *in an interior point*, all
	we can deduce is that
	the interiors intersect.  (We can NOT deduce that the exteriors intersect,
	since some other segments in the geometries might cover the points in the
	neighbourhood of the intersection.)
	It is important that the point be known to be an interior point of
	both Geometries, since it is possible in a self-intersecting geometry to
	have a proper intersection on one segment that is also a boundary point of another segment.
	*/
	else if (dimA==1 && dimB==1) {
		if (hasProperInterior) im->setAtLeast("0FFFFFFFF");
	}
}

/**
* Copy all nodes from an arg geometry into this graph.
* The node label in the arg geometry overrides any previously computed
* label for that argIndex.
* (E.g. a node may be an intersection node with
* a computed label of BOUNDARY,
* but in the original arg Geometry it is actually
* in the interior due to the Boundary Determination Rule)
*/
void RelateComputer::copyNodesAndLabels(int argIndex) {
	map<Coordinate,Node*,CoordLT> nMap(arg->at(argIndex)->getNodeMap()->nodeMap);
	map<Coordinate,Node*,CoordLT>::iterator nodeIt;
	for(nodeIt=nMap.begin();nodeIt!=nMap.end();nodeIt++) {
		Node *graphNode=nodeIt->second;
		Node *newNode=nodes->addNode(graphNode->getCoordinate());
		newNode->setLabel(argIndex,graphNode->getLabel()->getLocation(argIndex));
		//node.print(System.out);
	}
}


/**
* Insert nodes for all intersections on the edges of a Geometry.
* Label the created nodes the same as the edge label if they do not already have a label.
* This allows nodes created by either self-intersections or
* mutual intersections to be labelled.
* Endpoint nodes will already be labelled from when they were inserted.
*/
void RelateComputer::computeIntersectionNodes(int argIndex) {
	vector<Edge*> edges=arg->at(argIndex)->getEdges();
	for(vector<Edge*>::iterator i=edges.begin();i<edges.end();i++) {
		Edge *e=*i;
		int eLoc=e->getLabel()->getLocation(argIndex);
		vector<EdgeIntersection*> eiL(e->getEdgeIntersectionList()->list);
		for(vector<EdgeIntersection*>::iterator eiIt=eiL.begin();eiIt<eiL.end();eiIt++) {
			EdgeIntersection *ei=*eiIt;
			RelateNode *n=(RelateNode*) nodes->addNode(ei->coord);
			if (eLoc==Location::BOUNDARY)
				n->setLabelBoundary(argIndex);
			else {
				if (n->getLabel()->isNull(argIndex))
					n->setLabel(argIndex,Location::INTERIOR);
			}
			//Debug.println(n);
		}
	}
}

/**
* For all intersections on the edges of a Geometry,
* label the corresponding node IF it doesn't already have a label.
* This allows nodes created by either self-intersections or
* mutual intersections to be labelled.
* Endpoint nodes will already be labelled from when they were inserted.
*/
void RelateComputer::labelIntersectionNodes(int argIndex) {
	vector<Edge*> edges=arg->at(argIndex)->getEdges();
	for(vector<Edge*>::iterator i=edges.begin();i<edges.end();i++) {
		Edge *e=*i;
		int eLoc=e->getLabel()->getLocation(argIndex);
		vector<EdgeIntersection*> eiL(e->getEdgeIntersectionList()->list);
		for(vector<EdgeIntersection*>::iterator eiIt=eiL.begin();eiIt<eiL.end();eiIt++) {
			EdgeIntersection *ei=*eiIt;
			RelateNode *n=(RelateNode*) nodes->find(ei->coord);
			if (n->getLabel()->isNull(argIndex)) {
				if (eLoc==Location::BOUNDARY)
					n->setLabelBoundary(argIndex);
				else
					n->setLabel(argIndex,Location::INTERIOR);
			}
			//n.print(System.out);
		}
	}
}

/**
* If the Geometries are disjoint, we need to enter their dimension and
* boundary dimension in the Ext rows in the IM
*/
void RelateComputer::computeDisjointIM(IntersectionMatrix *im) {
	Geometry *ga=arg->at(0)->getGeometry();
	if (!ga->isEmpty()) {
		im->set(Location::INTERIOR,Location::EXTERIOR,ga->getDimension());
		im->set(Location::BOUNDARY,Location::EXTERIOR,ga->getBoundaryDimension());
	}
	Geometry *gb=arg->at(1)->getGeometry();
	if (!gb->isEmpty()) {
		im->set(Location::EXTERIOR,Location::INTERIOR,gb->getDimension());
		im->set(Location::EXTERIOR,Location::BOUNDARY,gb->getBoundaryDimension());
	}
}

/**
* Check all nodes to see if their labels are consistent.
* If any are not, return false
*/
bool RelateComputer::isNodeEdgeAreaLabelsConsistent() {
	map<Coordinate,Node*,CoordLT> nMap(nodes->nodeMap);
	map<Coordinate,Node*,CoordLT>::iterator nodeIt;
	for(nodeIt=nMap.begin();nodeIt!=nMap.end();nodeIt++) {
		RelateNode *node=(RelateNode*) nodeIt->second;
		if (!node->getEdges()->isAreaLabelsConsistent()) {
			invalidPoint.setCoordinate(node->getCoordinate());
			return false;
		}
	}
	return true;
}

void RelateComputer::labelNodeEdges() {
	map<Coordinate,Node*,CoordLT> nMap(nodes->nodeMap);
	map<Coordinate,Node*,CoordLT>::iterator nodeIt;
	for(nodeIt=nMap.begin();nodeIt!=nMap.end();nodeIt++) {
		RelateNode *node=(RelateNode*) nodeIt->second;
		node->getEdges()->computeLabelling(*arg);
		//Debug.print(node.getEdges());
		//node.print(System.out);
	}
}

/**
* update the IM with the sum of the IMs for each component
*/
void RelateComputer::updateIM(IntersectionMatrix *im) {
	//Debug.println(im);
	for (vector<Edge*>::iterator ei=isolatedEdges.begin();ei<isolatedEdges.end();ei++) {
		Edge *e=*ei;
		e->GraphComponent::updateIM(im);
		//Debug.println(im);
	}
	map<Coordinate,Node*,CoordLT> nMap(nodes->nodeMap);
	map<Coordinate,Node*,CoordLT>::iterator nodeIt;
	for(nodeIt=nMap.begin();nodeIt!=nMap.end();nodeIt++) {
		RelateNode *node=(RelateNode*) nodeIt->second;
		node->updateIM(im);
		//Debug.println(im);
		node->updateIMFromEdges(im);
		//Debug.println(im);
		//node.print(System.out);
	}
}

/**
* Processes isolated edges by computing their labelling and adding them
* to the isolated edges list.
* Isolated edges are guaranteed not to touch the boundary of the target (since if they
* did, they would have caused an intersection to be computed and hence would
* not be isolated)
*/
void RelateComputer::labelIsolatedEdges(int thisIndex,int targetIndex) {
	vector<Edge*> edges=arg->at(thisIndex)->getEdges();
	for(vector<Edge*>::iterator i=edges.begin();i<edges.end();i++) {
		Edge *e=*i;
		if (e->isIsolated()) {
			labelIsolatedEdge(e,targetIndex,arg->at(targetIndex)->getGeometry());
			isolatedEdges.push_back(e);
		}
	}
}

/**
* Label an isolated edge of a graph with its relationship to the target geometry.
* If the target has dim 2 or 1, the edge can either be in the interior or the exterior.
* If the target has dim 0, the edge must be in the exterior
*/
void RelateComputer::labelIsolatedEdge(Edge *e,int targetIndex,Geometry *target){
	// this won't work for GeometryCollections with both dim 2 and 1 geoms
	if (target->getDimension()>0) {
		// since edge is not in boundary, may not need the full generality of PointLocator?
		// Possibly should use ptInArea locator instead?  We probably know here
		// that the edge does not touch the bdy of the target Geometry
		int loc=((PointLocator*) ptLocator)->locate(e->getCoordinate(),target);
		e->getLabel()->setAllLocations(targetIndex,loc);
	} else {
		e->getLabel()->setAllLocations(targetIndex,Location::EXTERIOR);
	}
	//System.out.println(e.getLabel());
}

/**
* Isolated nodes are nodes whose labels are incomplete
* (e.g. the location for one Geometry is null).
* This is the case because nodes in one graph which don't intersect
* nodes in the other are not completely labelled by the initial process
* of adding nodes to the nodeList.
* To complete the labelling we need to check for nodes that lie in the
* interior of edges, and in the interior of areas.
*/
void RelateComputer::labelIsolatedNodes() {
	map<Coordinate,Node*,CoordLT> nMap(nodes->nodeMap);
	map<Coordinate,Node*,CoordLT>::iterator nodeIt;
	for(nodeIt=nMap.begin();nodeIt!=nMap.end();nodeIt++) {
		Node *n=nodeIt->second;
		Label *label=n->getLabel();
		// isolated nodes should always have at least one geometry in their label
		Assert::isTrue(label->getGeometryCount()>0,"node with empty label found");
		if (n->isIsolated()) {
			if (label->isNull(0))
				labelIsolatedNode(n,0);
			else
				labelIsolatedNode(n,1);
		}
	}
}

/**
* Label an isolated node with its relationship to the target geometry.
*/
void RelateComputer::labelIsolatedNode(Node *n,int targetIndex) {
	int loc=((PointLocator*) ptLocator)->locate(n->getCoordinate(),arg->at(targetIndex)->getGeometry());
	n->getLabel()->setAllLocations(targetIndex,loc);
	//debugPrintln(n.getLabel());
}

