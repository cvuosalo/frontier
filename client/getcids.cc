/*
 * FroNTier client API
 * 
 * Author: Sergey Kosyakov
 *
 * $Header$
 *
 * $Id$
 *
 */
#include <frontier-cpp.h>

#include <iostream>
#include <stdexcept>
 

int main(int argc, char **argv)
 {
  if(argc!=4)
   {
    std::cout<<"Usage: "<<argv[0]<<" host port object_name"<<'\n';
    exit(1);
   }
      
  try
   {
    frontier::init();

    frontier::CDFDataSource ds(argv[1],atoi(argv[2]),"/Frontier/","");
    
    //ds.setReload(1);

    frontier::Request req("frontier_get_cid_list","1",frontier::BLOB,"table_name",argv[3]);

    std::vector<const frontier::Request*> vrq;
    vrq.insert(vrq.end(),&req);
    ds.getData(vrq); 

    ds.setCurrentLoad(1);
    int nrec=ds.getRecNum();
    std::cout<<"Got "<<nrec<<" records back."<<'\n';
    
    for(int i=0;i<nrec;i++)
     {
      long cid=ds.getLong();
      std::cout<<cid<<'\n';
     }
   }
  catch(std::exception& e)
   {
    std::cout << "Error: " << e.what() << "\n";
    exit(1);
   }
  catch(...)
   {
    std::cout << "Unknown exception\n";
    exit(1);
   }
 }

