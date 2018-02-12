//
//  cAbstractAccountant.hpp
//  avida-core (static)
//
//  Created by Matthew Rupp on 1/14/18.
//

#ifndef cResourceAcct_h
#define cResourceAcct_h

#include "cSpatialCountElem.h"
#include "cOffsetLinearGrid.h"
#include "resources/Types.h"

using cCellElements =  Apto::Array<cSpatialCountElem>;
using cResourceGrid = Apto::Array<ResAmount>;



class cResourceAcct
{ 
public:
  virtual ResAmount GetTotalAmount() const = 0;
  virtual void SetTotalAmount(const ResAmount& ) = 0;
  virtual ResAmount GetCellAmount(int cell_id) const = 0;
  virtual void AddResource(ResAmount amount) = 0;
  virtual void ScaleResource(double scale) = 0;
  virtual ResAmount operator[](int cell_id) const = 0;
  virtual ResAmount operator()(int cell_id) const = 0;
  virtual ResAmount operator()(int x, int y) const = 0;
  
  virtual void Update() = 0;
  
};



class cAbstractSpatialResourceAcct : public cResourceAcct
{
protected:
  int m_world_size_x;
  int m_world_size_y;
  int m_size_x;
  int m_size_y;
  cCellBox m_cellbox;
  cOffsetLinearGrid<ResAmount> m_abundance;
  
  
public:
  cAbstractSpatialResourceAcct(int size_x, int size_y, const cCellBox& cellbox)
  : m_world_size_x(size_x)
  , m_world_size_y(size_y)
  , m_size_x(cellbox.GetWidth())
  , m_size_y(cellbox.GetHeight())
  , m_cellbox(cellbox)
  , m_abundance(size_x, size_y, cellbox, 0.0)
  {
  }
  
  virtual ResAmount GetTotalAmount() const override;  
  virtual void SetTotalAmount(const ResAmount& val) override;

  virtual ResAmount GetCellAmount(int cell_id) const override;
  void SetCellAmount(int cell_id, ResAmount amount);
  
  virtual void AddResource(ResAmount amount) override;
  
  virtual void ScaleResource(double scale) override;
  
  virtual ResAmount operator[](int cell_id) const override;
  virtual ResAmount operator()(int cell_id) const override;
  virtual ResAmount operator()(int x, int y) const override;
  
  inline int GetSize() const;
};

ResAmount cAbstractSpatialResourceAcct::GetTotalAmount() const
{
  double sum = 0.0;
  for (int k = 0; k < m_abundance.GetSize(); k++){
    sum += m_abundance[k];
  }
  return sum;
}

void cAbstractSpatialResourceAcct::SetTotalAmount(const ResAmount& val)
{
  ResAmount frac = val / m_abundance.GetSize();
  for (int k = 0; k < m_abundance.GetSize(); k++){
    m_abundance[k] = frac;
  }
}


ResAmount cAbstractSpatialResourceAcct::GetCellAmount(int cell_id) const
{
  return m_abundance[cell_id];
}


void cAbstractSpatialResourceAcct::AddResource(ResAmount amount)
{
  ResAmount delta = amount / GetSize();
  for (int k=0; k<GetSize(); k++)
  {
    m_abundance[k] = (m_abundance[k] + delta >= 0.0) ? m_abundance[k] + delta : 0.0;
  }
}

void cAbstractSpatialResourceAcct::ScaleResource(double scale)
{
  assert(scale > 0.0);
  for (int k=0; k<GetSize(); k++)
  {
    m_abundance[k] *= m_abundance[k]*scale;
  }
}

void cAbstractSpatialResourceAcct::SetCellAmount(int cell_id, ResAmount amount) 
{ 
  m_abundance(cell_id) = amount;  //Will the compiler chose the one with the reference?
}

ResAmount cAbstractSpatialResourceAcct::operator[](int cell_id) const
{
  return m_abundance[cell_id];
}


ResAmount cAbstractSpatialResourceAcct::operator()(int cell_id) const
{
  return m_abundance[cell_id];
}

ResAmount cAbstractSpatialResourceAcct::operator()(int x, int y) const
{
  return m_abundance(x,y);
}

inline int cAbstractSpatialResourceAcct::GetSize() const
{
  return m_size_x * m_size_y;
}

#endif /* cAbstractAccountant_h */
