/*
 *  cAnalyzeUtil.cc
 *  Avida
 *
 *  Created by David on 12/2/05.
 *  Copyright 2005-2006 Michigan State University. All rights reserved.
 *  Copyright 1993-2003 California Institute of Technology.
 *
 */

#include "cAnalyzeUtil.h"

#include "cAvidaContext.h"
#include "defs.h"
#include "cEnvironment.h"
#include "cClassificationManager.h"
#include "cGenome.h"
#include "cGenomeUtil.h"
#include "cGenotype.h"
#include "cHardwareBase.h"
#include "cHardwareManager.h"
#include "cHistogram.h"
#include "cInstSet.h"
#include "cInstUtil.h"
#include "cLandscape.h"
#include "cOrganism.h"
#include "cPhenotype.h"
#include "cPopulation.h"
#include "cPopulationCell.h"
#include "cSpecies.h"
#include "cStats.h"
#include "cTestCPU.h"
#include "cTestUtil.h"
#include "cTools.h"
#include "cWorld.h"

#include <vector>

using namespace std;


void cAnalyzeUtil::TestGenome(cWorld* world, const cGenome & genome, cInstSet & inst_set,
                              ofstream& fp, int update)
{
  cTestCPU* testcpu = world->GetHardwareManager().CreateTestCPU();
  cAvidaContext& ctx = world->GetDefaultContext();
  
  cCPUTestInfo test_info;
  testcpu->TestGenome(ctx, test_info, genome);
  delete testcpu;
  
  cPhenotype &colony_phenotype = test_info.GetColonyOrganism()->GetPhenotype();
  fp << update << " "                                //  1
    << colony_phenotype.GetMerit().GetDouble() << " "            //  2
    << colony_phenotype.GetGestationTime() << " "             //  3
    << colony_phenotype.GetFitness() << " "                      //  4
    << 1.0 / (0.1  + colony_phenotype.GetGestationTime()) << " " //  5
    << genome.GetSize() << " "                                   //  6
    << colony_phenotype.GetCopiedSize() << " "                   //  7
    << colony_phenotype.GetExecutedSize() << endl;               //  8
}



void cAnalyzeUtil::TestInsSizeChangeRobustness(cWorld* world, ofstream& fp,
                                               const cInstSet & inst_set, const cGenome & in_genome,
                                               int num_trials, int update)
{
  cTestCPU* testcpu = world->GetHardwareManager().CreateTestCPU();
  cAvidaContext& ctx = world->GetDefaultContext();

  cCPUTestInfo test_info;
  const cInstruction inst_none = inst_set.GetInst("instruction_none");
  
  // Stats
  int num_viable = 0;
  int num_new_size = 0;
  int num_parent_size = 0;
  
  for (int i = 0; i < num_trials; i++) {
    cCPUMemory genome(in_genome);
    // Should check to only insert infront of an instruction (not a Nop)
    int ins_pos = -1;
    while (ins_pos < 0) {
      ins_pos = world->GetRandom().GetUInt(genome.GetSize());
      if( inst_set.IsNop(genome[ins_pos]) )  ins_pos = -1;
    }
    
    // Insert some "instruction_none" into the genome
    const int num_nops = world->GetRandom().GetUInt(5) + 5;
    for (int j = 0; j < num_nops; j++)  genome.Insert(ins_pos, inst_none);
    
    // Test the genome and output stats
    if (testcpu->TestGenome(ctx, test_info, genome)){ // Daughter viable...
      num_viable++;
      const double child_size =
        test_info.GetColonyOrganism()->GetGenome().GetSize();
      
      if (child_size == (double) in_genome.GetSize()) num_parent_size++;
      else if (child_size == (double) genome.GetSize()) num_new_size++;
    }
  } // for num_trials
  
  delete testcpu;
  
  fp << update << " "
    << (double) num_viable / num_trials << " "
    << (double) num_new_size / num_trials << " "
    << (double) num_parent_size / num_trials << " "
    << endl;
}



// Returns the genome of maximal fitness.
cGenome cAnalyzeUtil::CalcLandscape(cWorld* world, int dist, const cGenome & genome,
                                    cInstSet & inst_set)
{
  cAvidaContext& ctx = world->GetDefaultContext();

  cLandscape landscape(world, genome, inst_set);
  landscape.SetDistance(dist);
  landscape.Process(ctx);
  double peak_fitness = landscape.GetPeakFitness();
  cGenome peak_genome = landscape.GetPeakGenome();
  
  // Print the results.
  landscape.PrintStats(world->GetDataFileOFStream("landscape.dat"));
  landscape.PrintEntropy(world->GetDataFileOFStream("land-entropy.dat"));
  landscape.PrintSiteCount(world->GetDataFileOFStream("land-sitecount.dat"));
  
  // Repeat for Insertions...
  landscape.Reset(genome);
  landscape.ProcessInsert(ctx);
  landscape.PrintStats(world->GetDataFileOFStream("landscape-ins.dat"));
  landscape.PrintSiteCount(world->GetDataFileOFStream("land-ins-sitecount.dat"));
  if (landscape.GetPeakFitness() > peak_fitness) {
    peak_fitness = landscape.GetPeakFitness();
    peak_genome = landscape.GetPeakGenome();
  }
  
  // And Deletions...
  landscape.Reset(genome);
  landscape.ProcessDelete(ctx);
  landscape.PrintStats(world->GetDataFileOFStream("landscape-del.dat"));
  landscape.PrintSiteCount(world->GetDataFileOFStream("land-del-sitecount.dat"));
  if (landscape.GetPeakFitness() > peak_fitness) {
    peak_fitness = landscape.GetPeakFitness();
    peak_genome = landscape.GetPeakGenome();
  }
  
  return peak_genome;
}


void cAnalyzeUtil::PairTestLandscape(cWorld* world, const cGenome &genome, cInstSet &inst_set,
                                     int sample_size, int update)
{
  cAvidaContext& ctx = world->GetDefaultContext();

  cLandscape landscape(world, genome, inst_set);
  
  cString filename;
  filename.Set("pairtest.%d.dat", update);
  ofstream& fp = world->GetDataFileOFStream(filename);
  
  if (sample_size != 0) landscape.TestPairs(ctx, sample_size, fp);
  else landscape.TestAllPairs(ctx, fp);
  
  world->GetDataFileManager().Remove(filename);
}


void cAnalyzeUtil::CalcConsensus(cWorld* world, int lines_saved)
{
  const int num_inst = world->GetHardwareManager().GetInstSet().GetSize();
  const int update = world->GetStats().GetUpdate();
  cClassificationManager& classmgr = world->GetClassificationManager();
  
  // Setup the histogtams...
  cHistogram * inst_hist = new cHistogram[MAX_CREATURE_SIZE];
  for (int i = 0; i < MAX_CREATURE_SIZE; i++) inst_hist[i].Resize(num_inst,-1);
  
  // Loop through all of the genotypes adding them to the histograms.
  cGenotype * cur_genotype = classmgr.GetBestGenotype();
  for (int i = 0; i < classmgr.GetGenotypeCount(); i++) {
    const int num_organisms = cur_genotype->GetNumOrganisms();
    const int length = cur_genotype->GetLength();
    const cGenome & genome = cur_genotype->GetGenome();
    
    // Place this genotype into the histograms.
    for (int j = 0; j < length; j++) {
      assert(genome[j].GetOp() < num_inst);
      inst_hist[j].Insert(genome[j].GetOp(), num_organisms);
    }
    
    // Mark all instructions beyond the length as -1 in histogram...
    for (int j = length; j < MAX_CREATURE_SIZE; j++) {
      inst_hist[j].Insert(-1, num_organisms);
    }
    
    // ...and advance to the next genotype...
    cur_genotype = cur_genotype->GetNext();
  }
  
  // Now, lets print something!
  ofstream& fp = world->GetDataFileOFStream("consensus.dat");
  ofstream& fp_abundance = world->GetDataFileOFStream("con-abundance.dat");
  ofstream& fp_var = world->GetDataFileOFStream("con-var.dat");
  ofstream& fp_entropy = world->GetDataFileOFStream("con-entropy.dat");
  
  // Determine the length of the concensus genome
  int con_length;
  for (con_length = 0; con_length < MAX_CREATURE_SIZE; con_length++) {
    if (inst_hist[con_length].GetMode() == -1) break;
  }
  
  // Build the concensus genotype...
  cGenome con_genome(con_length);
  double total_entropy = 0.0;
  for (int i = 0; i < MAX_CREATURE_SIZE; i++) {
    const int mode = inst_hist[i].GetMode();
    const int count = inst_hist[i].GetCount(mode);
    const int total = inst_hist[i].GetCount();
    const double entropy = inst_hist[i].GetNormEntropy();
    if (i < con_length) total_entropy += entropy;
    
    // Break out if ALL creatures have a -1 in this area, and we've
    // finished printing all of the files.
    if (mode == -1 && count == total) break;
    
    if ( i < con_length )
      con_genome[i].SetOp(mode);
    
    // Print all needed files.
    if (i < lines_saved) {
      fp_abundance << count << " ";
      fp_var << inst_hist[i].GetCountVariance() << " ";
      fp_entropy << entropy << " ";
    }
  }
  
  // Put end-of-lines on the files.
  if (lines_saved > 0) {
    fp_abundance << endl;
    fp_var       << endl;
    fp_entropy   << endl;
  }
  
  // --- Study the consensus genome ---
  
  // Loop through genotypes again, and determine the average genetic distance.
  cur_genotype = classmgr.GetBestGenotype();
  cDoubleSum distance_sum;
  for (int i = 0; i < classmgr.GetGenotypeCount(); i++) {
    const int num_organisms = cur_genotype->GetNumOrganisms();
    const int cur_dist =
      cGenomeUtil::FindEditDistance(con_genome, cur_genotype->GetGenome());
    distance_sum.Add(cur_dist, num_organisms);
    
    // ...and advance to the next genotype...
    cur_genotype = cur_genotype->GetNext();
  }
  
  // Finally, gather last bits of data and print the results.
  cGenotype * con_genotype = classmgr.FindGenotype(con_genome, -1);
  const int best_dist = cGenomeUtil::FindEditDistance(con_genome,
                                                      classmgr.GetBestGenotype()->GetGenome());
  
  const double ave_dist = distance_sum.Average();
  const double var_dist = distance_sum.Variance();
  const double complexity_base = (double) con_genome.GetSize() - total_entropy;
  
  cString con_name;
  con_name.Set("classmgr/%03d-consensus-u%i.gen", con_genome.GetSize(),update);
  cTestUtil::PrintGenome(world, con_genome, con_name);
  
  
  if (con_genotype) {
    fp << update                                 << " " //  1
    << con_genotype->GetMerit()               << " " //  2
    << con_genotype->GetGestationTime()       << " " //  3
    << con_genotype->GetFitness()             << " " //  4
    << con_genotype->GetReproRate()           << " " //  5
    << con_genotype->GetLength()              << " " //  6
    << con_genotype->GetCopiedSize()          << " " //  7
    << con_genotype->GetExecutedSize()        << " " //  8
    << con_genotype->GetBirths()              << " " //  9
    << con_genotype->GetBreedTrue()           << " " // 10
    << con_genotype->GetBreedIn()             << " " // 11
    << con_genotype->GetNumOrganisms()        << " " // 12
    << con_genotype->GetDepth()               << " " // 13
    << con_genotype->GetID()                  << " " // 14
    << update - con_genotype->GetUpdateBorn() << " " // 15
    << best_dist                              << " " // 16
    << ave_dist                               << " " // 17
    << var_dist                               << " " // 18
    << total_entropy                          << " " // 19
    << complexity_base                        << " " // 20
    << endl;
  }
  else {
    cTestCPU* testcpu = world->GetHardwareManager().CreateTestCPU();
    cAvidaContext& ctx = world->GetDefaultContext();
    
    cCPUTestInfo test_info;
    testcpu->TestGenome(ctx, test_info, con_genome);
    delete testcpu;
    
    cPhenotype& colony_phenotype = test_info.GetColonyOrganism()->GetPhenotype();

    fp << update                                             << " "   //  1
      << colony_phenotype.GetMerit()                        << " "  //  2
      << colony_phenotype.GetGestationTime()                << " "  //  3
      << colony_phenotype.GetFitness()                      << " "  //  4
      << 1.0 / (0.1  + colony_phenotype.GetGestationTime()) << " "  //  5
      << con_genome.GetSize()                               << " "  //  6
      << colony_phenotype.GetCopiedSize()                   << " "  //  7
      << colony_phenotype.GetExecutedSize()                 << " "  //  8
      << 0                                  << " "  // Births       //  9
      << 0                                  << " "  // Breed True   // 10
      << 0                                  << " "  // Breed In     // 11
      << 0                                  << " "  // Num CPUs     // 12
      << -1                                 << " "  // Depth        // 13
      << -1                                 << " "  // ID           // 14
      << 0                                  << " "  // Age          // 15
      << best_dist                                          << " "  // 16
      << ave_dist                                           << " "  // 17
      << var_dist                                           << " "  // 18
      << total_entropy                                      << " "  // 19
      << complexity_base                                    << " "  // 20
      << endl;
  }
  
  // Flush the file...
  fp.flush();
  
  delete [] inst_hist;
}



/**
* This function goes through all creatures in the soup, and saves the
 * basic landscape data (neutrality, fitness, and so on) into a stream.
 *
 * @param fp The stream into which the data should be saved.
 *
 * @param sample_prob The probability with which a particular creature should
 * be analyzed (a value of 1 analyzes all creatures, a value of 0.1 analyzes
                * 10%, and so on).
 *
 * @param landscape A bool that indicates whether the creatures should be
 * landscaped (calc. neutrality and so on) or not.
 *
 * @param save_genotype A bool that indicates whether the creatures should
 * be saved or not.
 **/

void cAnalyzeUtil::AnalyzePopulation(cWorld* world, ofstream& fp,
                                     double sample_prob, bool landscape, bool save_genotype)
{
  cPopulation* pop = &world->GetPopulation();
  fp << "# (1) cell number (2) genotype name (3) length (4) fitness [test-cpu] (5) fitness (actual) (6) merit (7) no of breed trues occurred (8) lineage label (9) neutral metric (10) -... landscape data" << endl;
  
  cAvidaContext& ctx = world->GetDefaultContext();

  const double skip_prob = 1.0 - sample_prob;
  for (int i = 0; i < pop->GetSize(); i++) {
    if (pop->GetCell(i).IsOccupied() == false) continue;  // No organism...
    if (world->GetRandom().P(skip_prob)) continue;               // Not sampled...
    
    cOrganism * organism = pop->GetCell(i).GetOrganism();
    cGenotype * genotype = organism->GetGenotype();
    const cGenome & genome = organism->GetGenome();
    
    cString creature_name;
    if ( genotype->GetThreshold() ) creature_name = genotype->GetName();
    else creature_name.Set("%03d-no_name-u%i-c%i", genotype->GetLength(),
                           world->GetStats().GetUpdate(), i );
    
    fp << i                                     << " "  // 1 cell ID
      << creature_name                       << " "  // 2 name
      << genotype->GetLength()                 << " "  // 3 length
      << genotype->GetTestFitness()            << " "  // 4 fitness (test-cpu)
      << organism->GetPhenotype().GetFitness() << " "  // 5 fitness (actual)
      << organism->GetPhenotype().GetMerit()   << " "  // 6 merit
      << genotype->GetBreedTrue()              << " "  // 7 breed true?
      << organism->GetLineageLabel()           << " "  // 8 lineage label
      << organism->GetPhenotype().GetNeutralMetric() << " "; // 9 neut metric
    
    // create landscape object for this creature
    if (landscape &&  genotype->GetTestFitness() > 0) {
      cLandscape landscape(world, genome, world->GetHardwareManager().GetInstSet());
      landscape.SetDistance(1);
      landscape.Process(ctx);
      landscape.PrintStats(fp);
    }
    else fp << endl;
    if ( save_genotype ){
      char filename[40];
      sprintf(filename, "classmgr/%s", static_cast<const char*>(creature_name));
      cTestUtil::PrintGenome(world, genome, filename);
    }
  }
}


/**
* This function prints out fitness data. The main point is that it
 * calculates the average fitness from info from the testCPU + the actual
 * merit of the organisms, and assigns zero fitness to those organisms
 * that will never reproduce.
 *
 * The function also determines the maximum fitness genotype, and can
 * produce fitness histograms.
 *
 * @param datafp A stream into which the fitness data should be written.
 * @param histofp A stream into which the fitness histogram should be
 * written.
 * @param histo_testCPU_fp A stream into which the fitness histogram as
 * determined exclusively from the test-CPU should be written.
 * @param save_max_f_genotype A bool that determines whether the genotype
 * with the maximum fitness should be saved into the classmgr.
 * @param print_fitness_histo A bool that determines whether fitness
 * histograms should be written.
 * @param hist_fmax The maximum fitness value to be taken into account
 * for the fitness histograms.
 * @param hist_fstep The width of the individual bins in the fitness
 * histograms.
 **/

void cAnalyzeUtil::PrintDetailedFitnessData(cWorld* world, cString& datafn,
                                            cString& hist_fn, cString& histo_testCPU_fn, bool save_max_f_genotype,
                                            bool print_fitness_histo, double hist_fmax, double hist_fstep)
{
  cPopulation* pop = &world->GetPopulation();
  const int update = world->GetStats().GetUpdate();
  const double generation = world->GetStats().SumGeneration().Average();
  
  // the histogram variables
  vector<int> histo;
  vector<int> histo_testCPU;
  int bins;
  
  if ( print_fitness_histo ){
    bins = (int) (hist_fmax / hist_fstep) + 1;
    histo.resize( bins, 0 ); // resize and clear
    histo_testCPU.resize( bins, 0 );
  }
  
  int n = 0;
  int nhist_tot = 0;
  int nhist_tot_testCPU = 0;
  double fave = 0;
  double fave_testCPU = 0;
  double max_fitness = -1; // we set this to -1, so that even 0 is larger...
  cGenotype * max_f_genotype = NULL;
  
  cTestCPU* testcpu = world->GetHardwareManager().CreateTestCPU();
  cAvidaContext& ctx = world->GetDefaultContext();

  for (int i = 0; i < pop->GetSize(); i++) {
    if (pop->GetCell(i).IsOccupied() == false) continue;  // One use organisms.
    
    cOrganism * organism = pop->GetCell(i).GetOrganism();
    cGenotype * genotype = organism->GetGenotype();
    
    cCPUTestInfo test_info;
    testcpu->TestGenome(ctx, test_info, genotype->GetGenome());
    // We calculate the fitness based on the current merit,
    // but with the true gestation time. Also, we set the fitness
    // to zero if the creature is not viable.
    const double f = ( test_info.IsViable() ) ? organism->GetPhenotype().GetMerit().CalcFitness(test_info.GetTestOrganism()->GetPhenotype().GetGestationTime()) : 0;
    const double f_testCPU = test_info.GetColonyFitness();
    
    // Get the maximum fitness in the population
    // Here, we want to count only organisms that can truly replicate,
    // to avoid complications
    if ( f_testCPU > max_fitness &&
         test_info.GetTestOrganism()->GetPhenotype().CopyTrue() ){
      max_fitness = f_testCPU;
      max_f_genotype = genotype;
    }
    
    fave += f;
    fave_testCPU += f_testCPU;
    n += 1;
    
    
    // histogram
    if ( print_fitness_histo && f < hist_fmax ){
      histo[(int) (f / hist_fstep)] += 1;
      nhist_tot += 1;
    }
    
    if ( print_fitness_histo && f_testCPU < hist_fmax ){
      histo_testCPU[(int) (f_testCPU / hist_fstep)] += 1;
      nhist_tot_testCPU += 1;
    }
  }
  
  delete testcpu;
  
  // determine the name of the maximum fitness genotype
  cString max_f_name;
  if ( max_f_genotype->GetThreshold() )
    max_f_name = max_f_genotype->GetName();
  else // we put the current update into the name, so that it becomes unique.
    max_f_name.Set("%03d-no_name-u%i", max_f_genotype->GetLength(),
                   update );
  
  world->GetDataFileOFStream(datafn)
    << update                    << " "  // 1 update
    << generation                << " "  // 2 generation
    << fave/ (double) n          << " "  // 3 average fitness
    << fave_testCPU/ (double) n  << " "  // 4 average test fitness
    << n 	                      << " "  // 5 organism total
    << max_fitness               << " "  // 6 maximum fitness
    << max_f_name	      << " "  // 7 maxfit genotype name
    << endl;
  
  if (save_max_f_genotype) {
    char filename[40];
    sprintf(filename, "classmgr/%s", static_cast<const char*>(max_f_name));
    cTestUtil::PrintGenome(world, max_f_genotype->GetGenome(), filename);
  }
  
  if (print_fitness_histo) {
    ofstream& hist_fp = world->GetDataFileOFStream(hist_fn);
    hist_fp
      << update            << " "  // 1 update
	    << generation        << " "  // 2 generation
	    << fave/ (double) n  << " "; // 3 average fitness
    
    // now output the fitness histo
    vector<int>::const_iterator it = histo.begin();
    for ( ; it != histo.end(); it++ ) hist_fp << (double) (*it) / (double) nhist_tot << " ";
    hist_fp << endl;
    
    ofstream& histo_testCPU_fp = world->GetDataFileOFStream(histo_testCPU_fn);
    histo_testCPU_fp
      << update                    << " "  // 1 update
      << generation                << " "  // 2 generation
      << fave_testCPU/ (double) n  << " "; // 3 average fitness
    
    // now output the fitness histo
    it = histo_testCPU.begin();
    for (; it != histo_testCPU.end(); it++) histo_testCPU_fp << (double) (*it) / (double) nhist_tot_testCPU << " ";
    histo_testCPU_fp << endl;
  }
}


/**
* This function goes through all genotypes currently present in the soup,
 * and writes into an output file the average Hamming distance between the
 * creatures in the population and a given reference genome.
 *
 * @param fp The stream into which the data should be saved.
 * @param reference_genome The reference genome.
 * @param save_creatures A bool that indicates whether creatures should be
 * saved into the classmgr or not.
 **/

void cAnalyzeUtil::PrintGeneticDistanceData(cWorld* world, ofstream& fp,
                                            const char * creature_name)
{
  double hamming_m1 = 0;
  double hamming_m2 = 0;
  int count = 0;
  int dom_dist = 0;
  
  // load the reference genome
  cGenome reference_genome(cInstUtil::LoadGenome(creature_name, world->GetHardwareManager().GetInstSet()));
  
  // get the info for the dominant genotype
  cGenotype * cur_genotype = world->GetClassificationManager().GetBestGenotype();
  cGenome genome = cur_genotype->GetGenome();
  dom_dist = cGenomeUtil::FindHammingDistance( reference_genome, genome );
  hamming_m1 += dom_dist;
  hamming_m2 += dom_dist*dom_dist;
  count += cur_genotype->GetNumOrganisms();
  // now cycle over the remaining genotypes
  for (int i = 1; i < world->GetClassificationManager().GetGenotypeCount(); i++) {
    cur_genotype = cur_genotype->GetNext();
    cGenome genome = cur_genotype->GetGenome();
    
    int dist = cGenomeUtil::FindHammingDistance( reference_genome, genome );
    hamming_m1 += dist;
    hamming_m2 += dist*dist;
    count += cur_genotype->GetNumOrganisms();
  }
  
  hamming_m1 /= (double) count;
  hamming_m2 /= (double) count;
  
  fp << world->GetStats().GetUpdate()          << " "  // 1 update
    << hamming_m1 			     << " "  // ave. Hamming dist
    << sqrt( ( hamming_m2 - hamming_m1*hamming_m1 ) / (double) count )
    << " "  // std. error
    << cGenomeUtil::FindHammingDistance( reference_genome,
                                         world->GetClassificationManager().GetBestGenotype()->GetGenome() ) << " "
    << endl;
}


/**
* This function goes through all genotypes currently present in the soup,
 * and writes into an output file the names of the genotypes, the fitness
 * as determined in the test cpu, and the genetic distance to a reference
 * genome.
 *
 * @param fp The stream into which the data should be saved.
 * @param reference_genome The reference genome.
 * @param save_creatures A bool that indicates whether creatures should be
 * saved into the classmgr or not.
 **/

void cAnalyzeUtil::GeneticDistancePopDump(cWorld* world, ofstream& fp,
                                          const char * creature_name, bool save_creatures)
{
  double sum_fitness = 0;
  int sum_num_organisms = 0;
  
  // load the reference genome
  cGenome reference_genome( cInstUtil::LoadGenome(creature_name, world->GetHardwareManager().GetInstSet()) );
  
  // first, print out some documentation...
  fp << "# (1) genotype name (2) fitness [test-cpu] (3) abundance (4) Hamming distance to reference (5) Levenstein distance to reference" << endl;
  fp << "# reference genome is the START_CREATURE" << endl;
  
  // cycle over all genotypes
  cGenotype * cur_genotype = world->GetClassificationManager().GetBestGenotype();
  for (int i = 0; i < world->GetClassificationManager().GetGenotypeCount(); i++) {
    const cGenome & genome = cur_genotype->GetGenome();
    const int num_orgs = cur_genotype->GetNumOrganisms();
    
    // now output
    
    sum_fitness += cur_genotype->GetTestFitness() * num_orgs;
    sum_num_organisms += num_orgs;
    
    fp << cur_genotype->GetName()       << " "  // 1 name
      << cur_genotype->GetTestFitness()  << " "  // 2 fitness
      << num_orgs                        << " "  // 3 abundance
      << cGenomeUtil::FindHammingDistance(reference_genome, genome) << " "
      << cGenomeUtil::FindEditDistance(reference_genome, genome) << " "  // 5
      << genome.AsString()             << " "  // 6 genome
      << endl;
    
    // save into classmgr
    if (save_creatures) {
      char filename[40];
      sprintf(filename, "classmgr/%s", static_cast<const char*>(cur_genotype->GetName()) );
      cTestUtil::PrintGenome(world, genome, filename);
    }
    
    // ...and advance to the next genotype...
    cur_genotype = cur_genotype->GetNext();
  }
  fp << "# ave fitness from Test CPU's: "
    << sum_fitness/sum_num_organisms << endl;
}


/**
* This function goes through all creatures in the soup, and writes out
 * how many tasks the different creatures have done up to now. It counts
 * every task only once, i.e., if a creature does 'put' three times, that
 * will increase its count only by one.
 *
 * @param fp The file into which the result should be written.
 **/

void cAnalyzeUtil::TaskSnapshot(cWorld* world, ofstream& fp)
{
  cPopulation* pop = &world->GetPopulation();
  fp << "# (1) cell number\n# (2) number of rewarded tasks done so far\n# (3) total number of tasks done so far\n# (4) same as 2, but right before divide\n# (5) same as 3, but right before divide\n# (6) same as 2, but for parent\n# (7) same as 3, but for parent\n# (8) genotype fitness\n# (9) genotype name" << endl;
  
  cTestCPU* testcpu = world->GetHardwareManager().CreateTestCPU();
  cAvidaContext& ctx = world->GetDefaultContext();

  for (int i = 0; i < pop->GetSize(); i++) {
    if (pop->GetCell(i).IsOccupied() == false) continue;
    cOrganism * organism = pop->GetCell(i).GetOrganism();
    
    // create a test-cpu for the current creature
    cCPUTestInfo test_info;
    testcpu->TestGenome(ctx, test_info, organism->GetGenome());
    cPhenotype & test_phenotype = test_info.GetTestOrganism()->GetPhenotype();
    cPhenotype & phenotype = organism->GetPhenotype();
    
    int num_tasks = world->GetEnvironment().GetTaskLib().GetSize();
    int sum_tasks_all = 0;
    int sum_tasks_rewarded = 0;
    int divide_sum_tasks_all = 0;
    int divide_sum_tasks_rewarded = 0;
    int parent_sum_tasks_all = 0;
    int parent_sum_tasks_rewarded = 0;
    
    for (int j = 0; j < num_tasks; j++) {
      // get the number of bonuses for this task
      int bonuses = 1; //phenotype.GetTaskLib().GetTaskNumBonus(j);
      int task_count = ( phenotype.GetCurTaskCount()[j] == 0 ) ? 0 : 1;
      int divide_tasks_count = (test_phenotype.GetLastTaskCount()[j] == 0)?0:1;
      int parent_task_count = (phenotype.GetLastTaskCount()[j] == 0) ? 0 : 1;
      
      // If only one bonus, this task is not rewarded, as last bonus is + 0.
      if (bonuses > 1) {
        sum_tasks_rewarded += task_count;
        divide_sum_tasks_rewarded += divide_tasks_count;
        parent_sum_tasks_rewarded += parent_task_count;
      }
      sum_tasks_all += task_count;
      divide_sum_tasks_all += divide_tasks_count;
      parent_sum_tasks_all += parent_task_count;
    }
    
    fp << i                          << " " // 1 cell number
      << sum_tasks_rewarded         << " " // 2 number of tasks rewarded
      << sum_tasks_all              << " " // 3 total number of tasks done
      << divide_sum_tasks_rewarded  << " " // 4 num rewarded tasks on divide
      << divide_sum_tasks_all       << " " // 5 num total tasks on divide
      << parent_sum_tasks_rewarded  << " " // 6 parent number of tasks rewared
      << parent_sum_tasks_all       << " " // 7 parent total num tasks done
      << test_info.GetColonyFitness()         << " " // 8 genotype fitness
      << organism->GetGenotype()->GetName() << " " // 9 genotype name
      << endl;
  }
  
  delete testcpu;
}

void cAnalyzeUtil::TaskGrid(cWorld* world, ofstream& fp)
{ 
  cPopulation* pop = &world->GetPopulation();
  cTestCPU* testcpu = world->GetHardwareManager().CreateTestCPU();
  cAvidaContext& ctx = world->GetDefaultContext();

  for (int i = 0; i < pop->GetWorldX(); i++) {
    for (int j = 0; j < pop->GetWorldY(); j++) {
      int task_sum = 0;
      int cell_num = i*pop->GetWorldX()+j;
      if (pop->GetCell(cell_num).IsOccupied() == true) {
        cOrganism * organism = pop->GetCell(cell_num).GetOrganism();
        cCPUTestInfo test_info;
        testcpu->TestGenome(ctx, test_info, organism->GetGenome());
        cPhenotype & test_phenotype = test_info.GetTestOrganism()->GetPhenotype();
        int num_tasks = world->GetEnvironment().GetTaskLib().GetSize();   
        for (int k = 0; k < num_tasks; k++) {
          if (test_phenotype.GetLastTaskCount()[k]>0) {
            task_sum = task_sum + (int) pow(2.0,k); 
          } 
        }
      }
      fp << task_sum << " ";
    }
    fp << endl;
  }
  
  delete testcpu;
}

/**
* This function prints all the tasks that viable creatures have performed
 * so far (compare with the event 'print_task_data', which prints all tasks.
           **/

void cAnalyzeUtil::PrintViableTasksData(cWorld* world, ofstream& fp)
{
  const int num_tasks = world->GetNumTasks();
  cPopulation* pop = &world->GetPopulation();
  
  vector<int> tasks(num_tasks);
  vector<int>::iterator it;
  
  // clear task vector
  for (it = tasks.begin(); it != tasks.end(); it++)  (*it) = 0;
  
  for (int i = 0; i < pop->GetSize(); i++) {
    if (pop->GetCell(i).IsOccupied() == false) continue;
    if (pop->GetCell(i).GetOrganism()->GetGenotype()->GetTestFitness() > 0.0) {
      cPhenotype & phenotype = pop->GetCell(i).GetOrganism()->GetPhenotype();
      for (int j = 0; j < num_tasks; j++) {
        if (phenotype.GetCurTaskCount()[j] > 0)  tasks[j] += 1;
      }
    }
  }
  
  fp << world->GetStats().GetUpdate();
  for (it = tasks.begin(); it != tasks.end(); it++)  fp << " " << (*it);
  fp<<endl;
}


void cAnalyzeUtil::PrintTreeDepths(cWorld* world, ofstream& fp)
{
  // cycle over all genotypes
  cGenotype* genotype = world->GetClassificationManager().GetBestGenotype();
  for (int i = 0; i < world->GetClassificationManager().GetGenotypeCount(); i++) {
    fp << genotype->GetID() << " "             // 1
    << genotype->GetTestFitness() << " "    // 2
    << genotype->GetNumOrganisms() << " "   // 3
    << genotype->GetDepth() << " "          // 4
    << endl;
    
    // ...and advance to the next genotype...
    genotype = genotype->GetNext();
  }
}


void cAnalyzeUtil::PrintDepthHistogram(cWorld* world, ofstream& fp)
{
  // Output format:    update  min  max  histogram_values...
  int min = INT_MAX;
  int max = 0;
  assert(fp.good());
  
  // Two pass method
  
  // Loop through all genotypes getting min and max values
  cClassificationManager& classmgr = world->GetClassificationManager();
  cGenotype * cur_genotype = classmgr.GetBestGenotype();
  for (int i = 0; i < classmgr.GetGenotypeCount(); i++) {
    if (cur_genotype->GetDepth() < min) min = cur_genotype->GetDepth();
    if (cur_genotype->GetDepth() > max) max = cur_genotype->GetDepth();
    cur_genotype = cur_genotype->GetNext();
  }
  assert(max >= min);
  
  // Allocate the array for the bins (& zero)
  int * n = new int[max - min + 1];
  for (int i = 0; i < max - min + 1; i++) n[i] = 0;
  
  // Loop through all genotypes binning the values
  cur_genotype = classmgr.GetBestGenotype();
  for (int i = 0; i < classmgr.GetGenotypeCount(); i++) {
    n[cur_genotype->GetDepth() - min] += cur_genotype->GetNumOrganisms();
    cur_genotype = cur_genotype->GetNext();
  }
  
  // Actual output
  fp << world->GetStats().GetUpdate() << " "
    << min << " "
    << max;
  
  for (int i = 0; i < max - min + 1; i++)  fp << " " << n[i];
  fp<<endl;
}


void cAnalyzeUtil::PrintGenotypeAbundanceHistogram(cWorld* world, ofstream& fp)
{
  assert(fp.good());
  
  // Allocate array for the histogram & zero it
  tArray <int> hist(world->GetClassificationManager().GetBestGenotype()->GetNumOrganisms());
  for (int i = 0; i < hist.GetSize(); i++) hist[i] = 0;
  
  // Loop through all genotypes binning the values
  cGenotype * cur_genotype = world->GetClassificationManager().GetBestGenotype();
  for (int i = 0; i < world->GetClassificationManager().GetGenotypeCount(); i++) {
    assert( cur_genotype->GetNumOrganisms() - 1 >= 0 );
    assert( cur_genotype->GetNumOrganisms() - 1 < hist.GetSize() );
    hist[cur_genotype->GetNumOrganisms() - 1]++;
    cur_genotype = cur_genotype->GetNext();
  }
  
  // Actual output
  fp << world->GetStats().GetUpdate() << " ";
  for (int i = 0; i < hist.GetSize(); i++)  fp<<hist[i]<<" ";
  fp << endl;
}


void cAnalyzeUtil::PrintSpeciesAbundanceHistogram(cWorld* world, ofstream& fp)
{
  int max = 0;
  assert(fp.good());
  
  // Find max species abundance...
  cClassificationManager& classmgr = world->GetClassificationManager();
  cSpecies * cur_species = classmgr.GetFirstSpecies();
  for (int i = 0; i < classmgr.GetNumSpecies(); i++) {
    if (max < cur_species->GetNumOrganisms()) {
      max = cur_species->GetNumOrganisms();
    }
    cur_species = cur_species->GetNext();
  }
  
  // Allocate array for the histogram & zero it
  tArray <int> hist(max);
  for (int i = 0; i < hist.GetSize(); i++)  hist[i] = 0;
  
  // Loop through all species binning the values
  cur_species = classmgr.GetFirstSpecies();
  for (int i = 0; i < classmgr.GetNumSpecies(); i++) {
    assert( cur_species->GetNumOrganisms() - 1 >= 0 );
    assert( cur_species->GetNumOrganisms() - 1 < hist.GetSize() );
    hist[cur_species->GetNumOrganisms() -1]++;
    cur_species = cur_species->GetNext();
  }
  
  // Actual output
  fp << world->GetStats().GetUpdate() << " ";
  for (int i = 0; i < hist.GetSize(); i++)  fp<<hist[i]<<" ";
  fp<<endl;
}


// this adds support for evan dorn's InstructionHistogramEvent.  -- kgn
/**
* Count the number of each instruction present in the population.  Output
 * this info to a log file
 **/
void cAnalyzeUtil::PrintInstructionAbundanceHistogram(cWorld* world, ofstream& fp)
{
  cPopulation* pop = &world->GetPopulation();
  int i,x,y;
  int num_inst=0;
  int mem_size=0;
  int * inst_counts;
  cCPUMemory cpu_mem; // cCPUMemory is a subclass of cGenome
  assert(fp.good());
  
  // ----- number of instructions available?
  num_inst = world->GetNumInstructions();
  inst_counts= new int[num_inst];
  
  // ----- create and initialize counting array
  inst_counts  = new int[num_inst];
  for (i=0;i<num_inst;i++)
  { inst_counts[i]=0;
  }
  
  int num_cells = pop->GetSize();
  //looping through all CPUs counting up instructions
  for( x=0; x<num_cells; x++ )
  { cPopulationCell & cell=pop->GetCell(x);
    if (cell.IsOccupied())
    {
      // access this CPU's code block
      cpu_mem=cell.GetOrganism()->GetHardware().GetMemory();
      mem_size=cpu_mem.GetSize();
      for (y=0; y<mem_size ; y++)
      { inst_counts[(cpu_mem[y]).GetOp()]++;     
      }  
    }
  }
  
  // ----- output instruction counts
  for(i=0; i<num_inst; i++)
  { fp<<inst_counts[i]<<" ";
  }
  fp<<endl;
}

