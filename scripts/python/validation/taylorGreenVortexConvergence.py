#!/usr/bin/env python

# file: taylorGreenVortexConvergence.py
# author: Olivier Mesnard (mesnardo@gwu.edu)
# description: Plots the grid-convergence for the Taylor-Green vortex case.


import os
import sys
import argparse
import math

import numpy
from matplotlib import pyplot

sys.path.append('{}/scripts/python'.format(os.environ['PETIBM_DIR']))
import ioPetIBM


def read_inputs():
  """Parses the command-line."""
  # create parser
  parser = argparse.ArgumentParser(description='Convergence for the '
                                               'Taylor-Green vortex case',
                        formatter_class= argparse.ArgumentDefaultsHelpFormatter)
  # fill parser with arguments
  parser.add_argument('--directory', dest='directory', type=str,
                      default=os.getcwd(),
                      help='directory containing the simulation folders')
  parser.add_argument('--Re', '-Re', dest='Re', type=float, default=100.0,
                      help='Reynolds number of the simulation')
  parser.add_argument('--time', '-t', dest='time', type=float, default=0.5,
                      help='time at which the error will be computed')
  parser.add_argument('--time-step', '-ts', dest='time_step', type=int, 
                      default=1000,
                      help='time-step at which the solution will be read')
  parser.add_argument('--amplitude', '-a', dest='amplitude', type=float, 
                      default=1.0, help='amplitude of the Taylor-Green vortex')
  parser.add_argument('--no-save', dest='save', action='store_false',
                      help='does not save the figure')
  parser.add_argument('--output', '-o', dest='output', type=str, 
                      default='grid_convergence',
                      help='name of the .png file saved')
  parser.add_argument('--no-show', dest='show', action='store_false',
                      help='does not display the figure')
  parser.set_defaults(save=True, show=True)
  # parse command-line
  return parser.parse_args()


def compute_order(ratio, coarse, medium, fine):
  """Computes the observed order of convergence 
  using the solution on three grids.

  Arguments
  ---------
  ratio -- grid-refinement ratio
  coarse, medium, fine -- solutions on three consecutive grids 
                          restricted on the coarsest grid
  """
  return ( math.log(numpy.linalg.norm(medium-coarse)
                    /numpy.linalg.norm(fine-medium))
           /math.log(ratio) )


def restriction(fine, coarse):
  """Restriction of the solution from a fine grid onto a coarse grid.

  Arguments
  ---------
  fine, coarse -- fine and coarse numerical solutions
  """
  def intersection(a, b, tolerance=1.0E-06):
    return numpy.any(numpy.abs(a-b[:, numpy.newaxis]) <= tolerance, axis=0)
  mask_x = intersection(fine['x'], coarse['x'])
  mask_y = intersection(fine['y'], coarse['y'])
  return {'x': fine['x'][mask_x],
          'y': fine['y'][mask_y],
          'values': numpy.array([fine['values'][j][mask_x] 
                                 for j in xrange(fine['y'].size) if mask_y[j]])}


def taylor_green_vortex(x, y, V=1.0, time=0.0, Re=100.0):
  """Computes the analytical solution of the 2D Taylor-Green vortex.

  Arguments
  ---------
  x, y -- coordinates in the x- and y- directions
  V -- amplitude of the sinusoidal velocity field (default 1.0)
  time -- time at which the solution is computed (default 0.0)
  Re -- Reynolds number of the flow (default 100.0)
  """
  x = 2.0*math.pi*(x-x[0])/(x[-1]-x[0])
  y = 2.0*math.pi*(y-y[0])/(y[-1]-y[0])
  X, Y = numpy.meshgrid(x, y)
  u = +V*numpy.sin(X)*numpy.cos(Y)*math.exp(-2.0*time/Re)
  v = -V*numpy.cos(X)*numpy.sin(Y)*math.exp(-2.0*time/Re)
  p = 0.25*(numpy.cos(2.0*X)+numpy.cos(2.0*Y))*math.exp(-4.0*time/Re)
  w = 2.0*numpy.sin(X)*numpy.sin(Y)*math.exp(-2.0*time/Re)
  return u, v, p, w


def main():
  """Plots the grid convergence for the Taylor-Green vortex case."""
  # parse command-line
  parameters = read_inputs()

  # initialization
  simulations = sorted(int(directory) 
                       for directory in os.listdir(parameters.directory)
                       if os.path.isdir('/'.join([parameters.directory, directory])))
  cases = numpy.empty(len(simulations), dtype=dict) 
  for i, case in enumerate(cases):
    cases[i] = {'directory': '{}/{}'.format(parameters.directory, simulations[i]),
                'grid-size': '{0}x{0}'.format(simulations[i])}

  for i, case in enumerate(cases):
    print('\n[case] grid-size: {}'.format(case['grid-size']))
    # read mesh grid
    x, y = ioPetIBM.read_grid(case['directory'])
    cases[i]['grid-spacing'] = (x[-1]-x[0])/(x.size-1)
    # read velocity and pressure fields
    cases[i]['u'], cases[i]['v'] = ioPetIBM.read_velocity(case['directory'], 
                                                          parameters.time_step, 
                                                          [x, y], 
                                                          periodic=['x', 'y'])
    cases[i]['p'] = ioPetIBM.read_pressure(case['directory'], 
                                           parameters.time_step, 
                                           [x, y])
    # compute analytical solution
    u_analytical, _, _, _ = taylor_green_vortex(case['u']['x'], case['u']['y'], 
                                                V=parameters.amplitude, 
                                                time=parameters.time , 
                                                Re=parameters.Re)
    _, v_analytical, _, _ = taylor_green_vortex(case['v']['x'], case['v']['y'], 
                                                V=parameters.amplitude, 
                                                time=parameters.time , 
                                                Re=parameters.Re)
    _, _, p_analytical, _ = taylor_green_vortex(case['p']['x'], case['p']['y'], 
                                                V=parameters.amplitude, 
                                                time=parameters.time , 
                                                Re=parameters.Re)
    # compute L2-norm error
    cases[i]['u']['error'] = (numpy.linalg.norm(case['u']['values']-u_analytical)
                              /numpy.linalg.norm(u_analytical))
    cases[i]['v']['error'] = (numpy.linalg.norm(case['v']['values']-v_analytical)
                              /numpy.linalg.norm(v_analytical))
    cases[i]['p']['error'] = (numpy.linalg.norm(case['p']['values']-p_analytical)
                              /numpy.linalg.norm(p_analytical))

  print('\nObserved order of convergence:')
  last_three = True
  coarse, medium, fine = ([cases[-3], cases[-2], cases[-1]] 
                          if last_three 
                          else [cases[0], cases[1], cases[2]])
  ratio = coarse['grid-spacing']/medium['grid-spacing']
  alpha = {'u': compute_order(ratio,
                              coarse['u']['values'],
                              restriction(medium['u'], coarse['u'])['values'],
                              restriction(fine['u'], coarse['u'])['values']),
           'v': compute_order(ratio,
                              coarse['v']['values'],
                              restriction(medium['v'], coarse['v'])['values'],
                              restriction(fine['v'], coarse['v'])['values']),
           'p': compute_order(ratio,
                              coarse['p']['values'],
                              restriction(medium['p'], coarse['p'])['values'],
                              restriction(fine['p'], coarse['p'])['values'])}
  print('\tu: {}'.format(alpha['u']))
  print('\tv: {}'.format(alpha['v']))
  print('\tp: {}'.format(alpha['p']))

  if parameters.save or parameters.show:
    print('\nPlot the grid convergence ...')
    pyplot.style.use('{}/scripts/python/style/'
                     'style_PetIBM.mplstyle'.format(os.environ['PETIBM_DIR']))
    pyplot.xlabel('cell-width')
    pyplot.ylabel('$L_2$-norm error')
    pyplot.plot([case['grid-spacing'] for case in cases], 
                [case['u']['error'] for case in cases], 
                label='u-velocity', marker='o')
    pyplot.plot([case['grid-spacing'] for case in cases], 
                [case['v']['error'] for case in cases],
                label='v-velocity', marker='o')
    pyplot.plot([case['grid-spacing'] for case in cases], 
                [case['p']['error'] for case in cases], 
                label='pressure', marker='o')
    h = numpy.linspace(cases[0]['grid-spacing'], cases[-1]['grid-spacing'], 101)
    pyplot.plot(h, h, label='$1^{st}$-order convergence', color='k')
    pyplot.plot(h, h**2, label='$2^{nd}$-order convergence', 
                color='k', linestyle='--')
    pyplot.legend()
    pyplot.xscale('log')
    pyplot.yscale('log')
    if parameters.save:
      pyplot.savefig('{}/{}.png'.format(parameters.directory, parameters.output))
    if parameters.show:
      pyplot.show()

  print('\n[{}] DONE'.format(os.path.basename(__file__)))


if __name__ == '__main__':
  main()