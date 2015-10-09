//
//  WebDriverActions.h
//  viewer-webed
//
//  Created by Matthew Rupp on 9/23/15.
//  Copyright (c) 2015 MSU. All rights reserved.
//

#ifndef viewer_webed_WebDriverActions_h
#define viewer_webed_WebDriverActions_h

class cActionLibrary;

#include <cmath>
#include <vector>
#include <algorithm>
#include <iterator>
#include <limits>

#include "avida/core/Feedback.h"
#include "avida/core/Types.h"
#include "cActionLibrary.h"

#include "avida/viewer/OrganismTrace.h"
#include "avida/private/systematics/Genotype.h"
#include "avida/private/systematics/Clade.h"


#include "cEnvironment.h"
#include "cOrganism.h"
#include "cPopulation.h"
#include "cWorld.h"

#include "Driver.h"
#include "WebDebug.h"
#include "WebDriverActions.h"

using namespace Avida::WebViewer;

class cWebAction : public cAction
{
  protected:
    Avida::Feedback& m_feedback;
  public:
    cWebAction(cWorld* world, const cString& args, Avida::Feedback& fb) : cAction(world,args) , m_feedback(fb) {}
};




class cWebActionPopulationStats : public cWebAction {
public:
  cWebActionPopulationStats(cWorld* world, const cString& args, Avida::Feedback& fb) : cWebAction(world, args, fb)
  {
  }
  static const cString GetDescription() { return "no arguments"; }
  void Process(cAvidaContext& ctx){
    //cerr << "webPopulationStats" << endl;
    const cStats& stats = m_world->GetStats();
    int update = stats.GetUpdate();;
    double ave_fitness = stats.GetAveFitness();;
    double ave_gestation_time = stats.GetAveGestation();
    double ave_metabolic_rate = stats.GetAveMerit();
    int org_count = stats.GetNumCreatures();
    double ave_age = stats.GetAveCreatureAge();
    
    /*
    cerr << update << " "
         << ave_fitness << " "
         << ave_gestation_time << " " 
         << ave_metabolic_rate << " "
         << org_count << " " 
         << ave_age << "  ?"
         << stats.GetTaskLastCountSize() << endl;
    */
    
    WebViewerMsg pop_data = {
      {"type", "data"}
      ,{"name", "webPopulationStats"}
      ,{"update", update}
      ,{"ave_fitness", ave_fitness}
      ,{"ave_gestation_time", ave_gestation_time}
      ,{"ave_metabolic_rate", ave_metabolic_rate}
      ,{"organisms", org_count}
      ,{"ave_age", ave_age}
    };
    
    cEnvironment& env = m_world->GetEnvironment();
    for (int t=0; t< env.GetNumTasks(); t++){
      pop_data[string(env.GetTask(t).GetName().GetData())] = 
        stats.GetTaskLastCount(t);
    }
    m_feedback.Data(pop_data.dump().c_str());
    //cerr << "\t\tdone." << endl;
  }
}; // cWebActionPopulationStats


class cWebActionOrgTraceBySequence : public cWebAction
{

  private:
  
    int m_seed;
    double m_mutation_rate;
    string m_sequence;
    
    string Int32ToBinary(unsigned int value){
      string retval = "";
      for (int i = 0; i < 32; i++)
        retval += ( (value >> i) & 1) ? "1" : "0";
      return retval;
    }
    
    json ParseSnapshot(const Viewer::HardwareSnapshot& s)
    {
      const std::string alphabet = "abcdefghijklmnopqrstuvwxyz";
      
      json j;
      j["didDivide"] = s.IsPostDivide();
      j["nextInstruction"] = s.NextInstruction().AsString().GetData();;

      std::map<std::string, std::string> regs;
      for (int i = 0; i < s.Registers().GetSize(); ++i)
        regs[alphabet.substr(i,1) + "x"] =
          Int32ToBinary(s.Register(i));
      j["registers"] = regs;
      
      std::map<std::string, std::vector<string>> bufs;
      for (auto it_i = s.Buffers().Begin(); it_i.Next();){
        vector<string> entries;
        for (auto it_j = it_i.Get()->Value2()->Begin(); it_j.Next();){
          entries.push_back(Int32ToBinary(*it_j.Get()));
        }
        bufs[it_i.Get()->Value1().GetData()] = entries;
      }
      j["buffers"] = bufs;
      
      std::map<std::string, int> functions;
      for (auto it = s.Functions().Begin(); it.Next();)
        functions[it.Get()->Value1().GetData()] = *(it.Get()->Value2());
      j["functions"] = functions;
      
      
      std::vector<std::map<string,int>> jumps;
      for (auto it = s.Jumps().Begin(); it.Next();){
        std::map<string,int> a_jump;
        const Viewer::HardwareSnapshot::Jump& this_jump = *(it.Get());
        a_jump["fromMemSpace"] = this_jump.from_mem_space;
        a_jump["fromIDX"] = this_jump.from_idx;
        a_jump["toMemSpace"] = this_jump.to_mem_space;
        a_jump["toIDX"] = this_jump.to_idx;
        a_jump["freq"] = this_jump.freq;
        jumps.push_back(a_jump);
      }
      j["jumps"] = jumps;
      
      std::vector<json> memspace;
      for (auto it = s.MemorySpace().Begin(); it.Next();){
        json this_space;
        this_space["label"] = it.Get()->label;
        vector<string> memory;
        for (auto it_mem = it.Get()->memory.Begin(); it_mem.Next();)
          memory.push_back(it_mem.Get()->GetSymbol().GetData());
        this_space["memory"] = memory;
        vector<int> mutated;
        for (auto it_mutated = it.Get()->mutated.Begin(); it_mutated.Next();)
          mutated.push_back( (*(it_mutated.Get())) ? 1 : 0 );
        this_space["mutated"] = mutated;
        std::map<string, int> heads;
        for (auto it_heads = it.Get()->heads.Begin(); it_heads.Next();)
          heads[it_heads.Get()->Value1().AsLower().GetData()] = *(it_heads.Get()->Value2());
        this_space["heads"] = heads;
        memspace.push_back(this_space);
      }
      j["memSpace"] = memspace;
      return j;
    }

  public:
    cWebActionOrgTraceBySequence(cWorld* world, const cString& args, Avida::Feedback& m_feedback)
    : cWebAction(world, args, m_feedback)
    {
      cString largs(args);
      if (largs.GetSize()){
        m_sequence = string(largs.PopWord().GetData());
        m_mutation_rate = (largs.GetSize()) ? largs.PopWord().AsDouble() : 0.0;
        m_seed = (largs.GetSize()) ? largs.PopWord().AsInt() : -1;
      } else {
        m_feedback.Warning("webOrgTraceBySequence: a genome sequence is a required argument.");
      }
    }
    
    static const cString GetDescription() { return "Arguments: GenomeSequence PointMutationRate Seed"; }
    
    void Process(cAvidaContext& ctx)
    {
      cerr << m_world << endl;
      cerr << "cWebActionOrtTraceBySequence::Process" << endl;
      cerr << "\tArguments: " << m_sequence << "," << m_mutation_rate 
           << "," << m_seed << endl;
      //Trace the genome sequence
      GenomePtr genome = 
        GenomePtr(new Genome(Apto::String( (m_sequence).c_str())));
      cerr << "\tAbout to Trace with settings " << m_world
           <<  "," << m_sequence << "," << m_mutation_rate << "," << m_seed;
      Viewer::OrganismTrace trace(m_world, genome, m_mutation_rate, m_seed);
      
      cerr << "\tTrace ready" << endl;
      vector<json> snapshots;
      WebViewerMsg retval = { 
                        {"type","data"},
                        {"name","webOrgTraceBySequence"}
                        };
                        
      for (int i = 0; i < trace.SnapshotCount(); ++i)
      {
        snapshots.push_back(ParseSnapshot(trace.Snapshot(i)));
      }
      retval["snapshots"] = snapshots;
      
      cerr << "\tAbout to make feedback" << endl;
      m_feedback.Data(WebViewerMsg(retval).dump().c_str());
    }
};  //End cWebActionOrgTraceBySequence



class cWebActionOrgDataByCellID : public cWebAction {
private:
  static constexpr int NO_SELECTION = -1;
  static constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  int m_cell_id;
  
  double GetTestFitness(Systematics::GenotypePtr& gptr) 
  {
    Apto::RNG::AvidaRNG rng(100); 
    cAvidaContext ctx(&m_world->GetDriver(),rng); 
    return Systematics::GenomeTestMetrics::GetMetrics(m_world,ctx,gptr)->GetFitness();
  }
  
  double GetTestMerit(Systematics::GenotypePtr& gptr) 
  {
    Apto::RNG::AvidaRNG rng(100); 
    cAvidaContext ctx(&m_world->GetDriver(),rng); 
    return Systematics::GenomeTestMetrics::GetMetrics(m_world,ctx,gptr)->GetMerit();
  }
  
  double GetTestGestationTime(Systematics::GenotypePtr& gptr) 
  {
    Apto::RNG::AvidaRNG rng(100); 
    cAvidaContext ctx(&m_world->GetDriver(),rng); 
    return Systematics::GenomeTestMetrics::GetMetrics(m_world,ctx,gptr)->GetGestationTime();
  }
  
  int GetTestTaskCount(Systematics::GenotypePtr& gptr, cString& task_name)
  {
    Apto::RNG::AvidaRNG rng(100); 
    cAvidaContext ctx(&m_world->GetDriver(),rng); 
    return Systematics::GenomeTestMetrics::GetMetrics(m_world,ctx,gptr)->GetGestationTime();
  }
  
  
public:
  cWebActionOrgDataByCellID(cWorld* world, const cString& args, Avida::Feedback& fb) : cWebAction(world, args, fb)
  {
    cString largs(args);
    int pop_size = m_world->GetPopulation().GetSize();
    if (largs.GetSize()){
      m_cell_id = largs.PopWord().AsInt();
    } else {
      m_feedback.Warning("webOrgDataByCellID requires a cell_id argument");
    }
    if (m_cell_id > pop_size || m_cell_id < 0){
      m_feedback.Warning("webOrgDataCellID cell_id is not within the appropriate range.");
    }
  }
  static const cString GetDescription() { return "Arguments: cellID"; }
  
  void Process(cAvidaContext& ctx)
  {
    if (m_cell_id == NO_SELECTION) return;
    cOrganism* org = m_world->GetPopulation().GetCell(m_cell_id).GetOrganism();
    
  
    WebViewerMsg data = { {"type","data"}, {"name","webOrgDataByCellID"} };
    
    if (org == nullptr){
      data["genotypeName"] = "-";
      data["fitness"] = nan;
      data["metabolism"] = nan;
      data["gestation"] = nan;
      data["age"] = nan;
      data["ancestor"] = nan;
      data["genome"] = "";
      data["isEstimate"] = false;
      data["tasks"] = {};
    } else {
      // We're going to emulate AvidaEd OSX right now and go through the
      // genotype rather than the organism for information.
      Systematics::GenotypePtr gptr;
      gptr.DynamicCastFrom(org->SystematicsGroup("genotype"));
      data["genotypeName"] = gptr->Properties().Get("name").StringValue().Substring(4).GetData();
      data["genome"] = gptr->Properties().Get("genome").StringValue().GetData();
      data["age"] = gptr->Properties().Get("update_born").IntValue();
      
      Systematics::CladePtr cptr;
      cptr.DynamicCastFrom(org->SystematicsGroup("clade"));
      data["ancestor"] = (cptr == nullptr) ? json(nan) : json(cptr->Properties().Get("name").StringValue());
      //TODO: Clades have names?  How?
      
      bool has_gestated = (gptr->Properties().Get("total_gestation_count").IntValue() > 0);
      data["isEstimate"] = (has_gestated) ? false : true;
      data["fitness"] = (has_gestated) ? gptr->Properties().Get("ave_fitness").DoubleValue() : GetTestFitness(gptr);
      data["metabolism"] = (has_gestated) ? gptr->Properties().Get("ave_metabolic_rate").DoubleValue() : GetTestMerit(gptr);
      data["gestation"] = (has_gestated) ? gptr->Properties().Get("ave_gestation").DoubleValue() : GetTestGestationTime(gptr);
      
      //TODO: It doesn't look like genotypes actually track task counts right now
      //Instead, AvidaEd goes through a test cpu to always grab the information
      map<string,double> task_count;
      cEnvironment& env = m_world->GetEnvironment();
      for (int t=0; t<env.GetNumTasks(); t++){
        cString task_name = env.GetTask(t).GetName();
        task_count[task_name.GetData()] = GetTestTaskCount(gptr, task_name);
      }
      data["tasks"] = task_count;
    }
    
    m_feedback.Data(data.dump().c_str());
      
  }
}; // cWebActionPopulationStats



class cWebActionGridData : public cWebAction {
  private:

    /*
      Need to use our own min/max_val functions
      because of nans.
    */
    double min_val(const vector<double>& vec)
    {
      if (vec.empty())
        return std::numeric_limits<double>::quiet_NaN();
      double min = vec[0];
      for (auto val : vec){
        if (!isfinite(val))
          continue;
        if (val < min || !isfinite(min))
          min = val;
      }
      return min;
    }
    
    double max_val(const vector<double>& vec)
    {
      if (vec.empty())
        return std::numeric_limits<double>::quiet_NaN();
      double max = vec[0];
      for (auto val : vec){
        if (!isfinite(val))
          continue;
        if (val > max || !isfinite(max))
          max = val;
      }
      return max;
    }
    
  
  public:
    cWebActionGridData(cWorld* world, const cString& args, Avida::Feedback& fb) : cWebAction(world,args,fb)
    {
    }
    
    static const cString GetDescription() { return "Arguments: none";}
    
    void Process(cAvidaContext& ctx)
    {
      WebViewerMsg data = { {"type","data"}, {"name","webGridData"} };
      cPopulation& population = m_world->GetPopulation();
      cEnvironment& env = m_world->GetEnvironment();
      vector<double> fitness;
      vector<double> gestation;
      vector<double> metabolism;
      vector<double> ancestor;
      map<string, vector<double>> tasks;
      double nan = std::numeric_limits<double>::quiet_NaN();
      vector<string> task_names;
      for (int t=0; t<env.GetNumTasks(); t++){
        task_names.push_back(string(env.GetTask(t).GetName().GetData()));
      }
      
      for (int i=0; i < population.GetSize(); i++)
      {
        cPopulationCell& cell = population.GetCell(i);
        cOrganism* org = cell.GetOrganism();
        
        
        if (org == nullptr){
          fitness.push_back(nan);
          gestation.push_back(nan);
          metabolism.push_back(nan);
          ancestor.push_back(nan);
          for (int t=0; t<env.GetNumTasks(); t++){
            if (tasks.find(task_names[t]) == tasks.end())
              tasks[task_names[t]] = vector<double>(1,0.0);
            else
              tasks[task_names[t]].push_back(0.0);
          }          
        } else {
          cPhenotype& phen = org->GetPhenotype();
          fitness.push_back(phen.GetFitness());
          gestation.push_back(phen.GetGestationTime());
          metabolism.push_back(phen.GetMerit().GetDouble());
          ancestor.push_back(org->GetLineageLabel());
          for (int t=0; t<env.GetNumTasks(); t++){
            if (tasks.find(task_names[t]) == tasks.end())
              tasks[task_names[t]] = vector<double>(1,phen.GetCurCountForTask(t));
            else
              tasks[task_names[t]].push_back(phen.GetCurCountForTask(t));
          }          
        }
      }
      data["fitness"] = { 
                  {"data",fitness}, 
                  {"minVal",min_val(fitness)}, 
                  {"maxVal",max_val(fitness)} 
                  };
      data["metabolism"] = {
                  {"data",metabolism}, 
                  {"minVal",min_val(metabolism)}, 
                  {"maxVal",max_val(metabolism)} 
                  };
      data["gestation"] = {
                  {"data",gestation}, 
                  {"minVal",min_val(gestation)}, 
                  {"maxVal",max_val(gestation)} 
                  };

      data["ancestor"] = {
                  {"data",ancestor}, 
                  {"minVal",min_val(ancestor)}, 
                  {"maxVal",max_val(ancestor)} 
                  };
                  
                  
      for (auto it : tasks){
        data[it.first] = {
          {"data",it.second},
          {"minVal",min_val(it.second)},
          {"maxVal",max_val(it.second)}
        };
      }
      cerr << "Size of message is: " << data.dump().size() << endl;
      m_feedback.Data(data.dump().c_str());
    }
};


void RegisterWebDriverActions(cActionLibrary* action_lib)
{
    action_lib->Register<cWebActionPopulationStats>("webPopulationStats");
    action_lib->Register<cWebActionOrgTraceBySequence>("webOrgTraceBySequence");
    action_lib->Register<cWebActionOrgDataByCellID>("webOrgDataByCellID");
    action_lib->Register<cWebActionGridData>("webGridData");
}

#endif
