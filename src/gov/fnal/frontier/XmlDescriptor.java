package gov.fnal.frontier;

import java.util.ArrayList;
import java.util.Enumeration;

/**
 * $Id$
 * $Author$
 * $Date$
 * $Revision$
 */

public class XmlDescriptor extends Descriptor {

    private String name         = null;
    private String version      = null;
    private String xsdVersion   = null;
    private String selectClause = null;
    private String fromClause   = null;
    private String finalClause  = null;
    private ArrayList attributes   = new ArrayList();
    private ArrayList wheres       = new ArrayList();

    XmlDescriptor(String aName, String aVersion, String aXsdVersion) {
	name       = aName;
	version    = aVersion;
	xsdVersion = aXsdVersion;
    }

    public void validate() throws LoaderException {
	String msg = "";
	if (name == null)
	    msg += "  No value was supplied with descriptor's type tag.";
	else if (version == null)
	    msg += "  No value was supplied with descriptor's version tag.";
	else if (xsdVersion == null)
	    msg += "  No value was supplied with descriptor's xsdversion tag.";
	else if (selectClause == null)
	    msg += " No data was supplied with the select tag.";
	else if (fromClause == null)
	    msg += " No data was supplied with the from tag.";
	if (!msg.equals(""))
	    throw new LoaderException(msg);
	for(int i=0;i<attributes.size();i++)
	    ((Attribute)  attributes.get(i)).validate();
	for(int i=0;i<wheres.size();i++)
	    ((WhereClause)  wheres.get(i)).validate();
    }

    public void setSelectClause(String aSelectClause) {
	selectClause = aSelectClause;
    }

    public String getSelectClause() {
	return selectClause;
    }

    public void setFromClause(String aFromClause) {
	fromClause = aFromClause;
    }

    public String getFromClause() {
	return fromClause;
    }

    public void setFinalClause(String aFinalClause) {
	finalClause = aFinalClause;
    }

    public String getFinalClause() {
	return finalClause;
    }

    public void addAttribute(String type, String field) throws LoaderException {
	attributes.add(new Attribute(type,field));
    }

    public WhereClause addWhereClause() {
	WhereClause where = new WhereClause();
	wheres.add(where);
	return where;
    }

    /**
     * Locates a where clause that matches the given Command.
     * Then builds an SQL where clause from the data supplied
     * in Command. 
     *
     * @param command An instance of the command class.  
     * @param response The servlet response we are producing
     * @exception IOException if an input/output error occurs
     * @exception ServletException if a servlet error occurs
     * @return String containg an executable SQL where clause. If
     * no data was found then null is returned.
     * 
     */
    public String findAndBuildWhereClause(Command command) {
	WhereClause matchingWhere = null;
	int numKeys = command.size();
	for(int i=0;i<wheres.size();i++) {
	    WhereClause where = (WhereClause) wheres.get(i);
	    // See if this where is a potential candidate
	    if (numKeys == where.getParamCount()) {
		Enumeration keys = command.keys();
		boolean match = true;
		while( keys.hasMoreElements() && match ) {
		    if ( !where.paramExists((String) keys.nextElement()))
			match = false;
		}
		if (match)
		    matchingWhere = where;
	    }
	}
	return matchingWhere.buildWhere(command);
    }

}
