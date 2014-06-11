template <>
PetscErrorCode TairaColoniusSolver<2>::generateBNQ()
{
	PetscErrorCode ierr;
	PetscInt       numProcs;
	PetscInt       mstart, nstart, m, n;
	PetscInt       *d_nnz, *o_nnz;
	PetscInt       qStart, qEnd, lambdaStart, lambdaEnd, qLocalSize, lambdaLocalSize;
	PetscInt       localIdx;
	PetscReal      **pGlobalIdx;
	PetscInt       row, cols[2], col, value;
	PetscReal      values[2] = {-1.0, 1.0};
	PetscReal      xCoord, yCoord, h;
	
	MPI_Comm_size(PETSC_COMM_WORLD, &numProcs);
	
	ierr = VecGetOwnershipRange(q, &qStart, &qEnd); CHKERRQ(ierr);
	qLocalSize = qEnd-qStart;

	// create arrays to store nnz values
	ierr = PetscMalloc(qLocalSize*sizeof(PetscInt), &d_nnz); CHKERRQ(ierr);
	ierr = PetscMalloc(qLocalSize*sizeof(PetscInt), &o_nnz); CHKERRQ(ierr);
	
	// ownership range of lambda
	ierr = VecGetOwnershipRange(lambda, &lambdaStart, &lambdaEnd); CHKERRQ(ierr);
	lambdaLocalSize = lambdaEnd-lambdaStart;

	ierr = DMDAVecGetArray(pda, pMapping, &pGlobalIdx); CHKERRQ(ierr);

	// determine the number of non-zeros in each row
	// in the diagonal and off-diagonal portions of the matrix
	localIdx = 0;
	// U
	ierr = DMDAGetCorners(uda, &mstart, &nstart, NULL, &m, &n, NULL); CHKERRQ(ierr);
	for(PetscInt j=nstart; j<nstart+n; j++)
	{
		yCoord = 0.5*(mesh->y[j] + mesh->y[j+1]);
		for(PetscInt i=mstart; i<mstart+m; i++)
		{
			h = mesh->dx[i];
			xCoord = mesh->x[i+1];
			// G portion
			cols[0] = pGlobalIdx[j][i];
			cols[1] = pGlobalIdx[j][i+1];
			countNumNonZeros(cols, 2, lambdaStart, lambdaEnd, d_nnz[localIdx], o_nnz[localIdx]);
			// ET portion
			for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
			{
				for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
				{
					if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h)
					{
						col = globalIndexMapping[*l];
						(col>=lambdaStart && col<lambdaEnd)? d_nnz[localIdx]++ : o_nnz[localIdx]++;
					}
				}
			}
			localIdx++;
		}
	}
	// V
	ierr = DMDAGetCorners(vda, &mstart, &nstart, NULL, &m, &n, NULL); CHKERRQ(ierr);
	for(PetscInt j=nstart; j<nstart+n; j++)
	{
		h = mesh->dy[j];
		yCoord = mesh->y[j+1];
		for(PetscInt i=mstart; i<mstart+m; i++)
		{
			xCoord = 0.5*(mesh->x[i] + mesh->x[i+1]);
			// G portion
			cols[0] = pGlobalIdx[j][i];
			cols[1] = pGlobalIdx[j+1][i];
			countNumNonZeros(cols, 2, lambdaStart, lambdaEnd, d_nnz[localIdx], o_nnz[localIdx]);
			// ET portion
			for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
			{
				for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
				{
					if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h)
					{
						col = globalIndexMapping[*l] + numBoundaryPointsOnProcess[procIdx];
						(col>=lambdaStart && col<lambdaEnd)? d_nnz[localIdx]++ : o_nnz[localIdx]++;
					}
				}
			}
			localIdx++;
		}
	}
	
	// allocate memory for the matrix
	ierr = MatCreate(PETSC_COMM_WORLD, &BNQ); CHKERRQ(ierr);
	ierr = MatSetType(BNQ, MATMPIAIJ); CHKERRQ(ierr);
	ierr = MatSetSizes(BNQ, qLocalSize, lambdaLocalSize, PETSC_DETERMINE, PETSC_DETERMINE); CHKERRQ(ierr);
	ierr = MatMPIAIJSetPreallocation(BNQ, 0, d_nnz, 0, o_nnz); CHKERRQ(ierr);

	// deallocate d_nnz and o_nnz
	ierr = PetscFree(d_nnz); CHKERRQ(ierr);
	ierr = PetscFree(o_nnz); CHKERRQ(ierr);

	// assemble matrix Q
	localIdx = 0;
	// U
	ierr = DMDAGetCorners(uda, &mstart, &nstart, NULL, &m, &n, NULL); CHKERRQ(ierr);
	for(PetscInt j=nstart; j<nstart+n; j++)
	{
		yCoord = 0.5*(mesh->y[j] + mesh->y[j+1]);
		for(PetscInt i=mstart; i<mstart+m; i++)
		{
			h = mesh->dx[i];
			xCoord = mesh->x[i+1];
			row = localIdx + qStart;
			// G portion
			cols[0] = pGlobalIdx[j][i];
			cols[1] = pGlobalIdx[j][i+1];
			ierr = MatSetValues(BNQ, 1, &row, 2, cols, values, INSERT_VALUES); CHKERRQ(ierr);
			// ET portion
			for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
			{
				for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
				{
					if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h)
					{
						col  = globalIndexMapping[*l];
						value= h*delta(xCoord-x[*l], yCoord-y[*l], h);
						ierr = MatSetValue(BNQ, row, col, value, INSERT_VALUES); CHKERRQ(ierr);
					}
				}
			}
			localIdx++;
		}
	}
	// V
	ierr = DMDAGetCorners(vda, &mstart, &nstart, NULL, &m, &n, NULL); CHKERRQ(ierr);
	for(PetscInt j=nstart; j<nstart+n; j++)
	{
		h = mesh->dy[j];
		yCoord = mesh->y[j+1];
		for(PetscInt i=mstart; i<mstart+m; i++)
		{
			xCoord = 0.5*(mesh->x[i] + mesh->x[i+1]);
			row = localIdx + qStart;
			// G portion
			cols[0] = pGlobalIdx[j][i];
			cols[1] = pGlobalIdx[j+1][i];
			ierr = MatSetValues(BNQ, 1, &row, 2, cols, values, INSERT_VALUES); CHKERRQ(ierr);
			// ET portion
			for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
			{
				for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
				{
					if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h)
					{
						col = globalIndexMapping[*l] + numBoundaryPointsOnProcess[procIdx];
						value= h*delta(xCoord-x[*l], yCoord-y[*l], h);
						ierr = MatSetValue(BNQ, row, col, value, INSERT_VALUES); CHKERRQ(ierr);
					}
				}
			}
			localIdx++;
		}
	}
	ierr = DMDAVecRestoreArray(pda, pMapping, &pGlobalIdx); CHKERRQ(ierr);

	ierr = MatAssemblyBegin(BNQ, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
	ierr = MatAssemblyEnd(BNQ, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);

	ierr = MatTranspose(BNQ, MAT_INITIAL_MATRIX, &QT); CHKERRQ(ierr);
	ierr = MatDiagonalScale(BNQ, BN, NULL); CHKERRQ(ierr);

	return 0;
}

template <>
PetscErrorCode TairaColoniusSolver<3>::generateBNQ()
{
	PetscErrorCode ierr;
	PetscInt       numProcs;
	PetscInt       mstart, nstart, pstart, m, n, p;
	PetscInt       *d_nnz, *o_nnz;
	PetscInt       qStart, qEnd, lambdaStart, lambdaEnd, qLocalSize, lambdaLocalSize;
	PetscInt       localIdx;
	PetscReal      ***pGlobalIdx;
	PetscInt       row, cols[2], col, value;
	PetscReal      values[2] = {-1.0, 1.0};
	PetscReal      xCoord, yCoord, zCoord, h;
	
	MPI_Comm_size(PETSC_COMM_WORLD, &numProcs);
	
	ierr = VecGetOwnershipRange(q, &qStart, &qEnd); CHKERRQ(ierr);
	qLocalSize = qEnd-qStart;

	// create arrays to store nnz values
	ierr = PetscMalloc(qLocalSize*sizeof(PetscInt), &d_nnz); CHKERRQ(ierr);
	ierr = PetscMalloc(qLocalSize*sizeof(PetscInt), &o_nnz); CHKERRQ(ierr);
	
	// ownership range of lambda
	ierr = VecGetOwnershipRange(lambda, &lambdaStart, &lambdaEnd); CHKERRQ(ierr);
	lambdaLocalSize = lambdaEnd-lambdaStart;

	ierr = DMDAVecGetArray(pda, pMapping, &pGlobalIdx); CHKERRQ(ierr);

	// determine the number of non-zeros in each row
	// in the diagonal and off-diagonal portions of the matrix
	localIdx = 0;
	// U
	ierr = DMDAGetCorners(uda, &mstart, &nstart, &pstart, &m, &n, &p); CHKERRQ(ierr);
	for(PetscInt k=pstart; k<pstart+p; k++)
	{
		zCoord = 0.5*(mesh->z[k] + mesh->z[k+1]);
		for(PetscInt j=nstart; j<nstart+n; j++)
		{
			yCoord = 0.5*(mesh->y[j] + mesh->y[j+1]);
			for(PetscInt i=mstart; i<mstart+m; i++)
			{
				h = mesh->dx[i];
				xCoord = mesh->x[i+1];
				// G portion
				cols[0] = pGlobalIdx[k][j][i];
				cols[1] = pGlobalIdx[k][j][i+1];
				countNumNonZeros(cols, 2, lambdaStart, lambdaEnd, d_nnz[localIdx], o_nnz[localIdx]);
				// ET portion
				for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
				{
					for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
					{
						if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h && fabs(zCoord-z[*l]) < 1.5*h)
						{
							col = globalIndexMapping[*l];
							(col>=lambdaStart && col<lambdaEnd)? d_nnz[localIdx]++ : o_nnz[localIdx]++;
						}
					}
				}
				localIdx++;
			}
		}
	}
	// V
	ierr = DMDAGetCorners(vda, &mstart, &nstart, &pstart, &m, &n, &p); CHKERRQ(ierr);
	for(PetscInt k=pstart; k<pstart+p; k++)
	{
		zCoord = 0.5*(mesh->z[k] + mesh->z[k+1]);
		for(PetscInt j=nstart; j<nstart+n; j++)
		{
			h = mesh->dy[j];
			yCoord = mesh->y[j+1];
			for(PetscInt i=mstart; i<mstart+m; i++)
			{
				xCoord = 0.5*(mesh->x[i] + mesh->x[i+1]);
				// G portion
				cols[0] = pGlobalIdx[k][j][i];
				cols[1] = pGlobalIdx[k][j+1][i];
				countNumNonZeros(cols, 2, lambdaStart, lambdaEnd, d_nnz[localIdx], o_nnz[localIdx]);
				// ET portion
				for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
				{
					for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
					{
						if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h && fabs(zCoord-z[*l]) < 1.5*h)
						{
							col = globalIndexMapping[*l] + numBoundaryPointsOnProcess[procIdx];
							(col>=lambdaStart && col<lambdaEnd)? d_nnz[localIdx]++ : o_nnz[localIdx]++;
						}
					}
				}
				localIdx++;
			}
		}
	}
	// W
	ierr = DMDAGetCorners(wda, &mstart, &nstart, &pstart, &m, &n, &p); CHKERRQ(ierr);
	for(PetscInt k=pstart; k<pstart+p; k++)
	{
		h = mesh->dz[k];
		zCoord = mesh->z[k+1];
		for(PetscInt j=nstart; j<nstart+n; j++)
		{
			yCoord = 0.5*(mesh->y[j] + mesh->y[j+1]);
			for(PetscInt i=mstart; i<mstart+m; i++)
			{
				xCoord = 0.5*(mesh->x[i] + mesh->x[i+1]);
				// G portion
				cols[0] = pGlobalIdx[k][j][i];
				cols[1] = pGlobalIdx[k+1][j][i];
				countNumNonZeros(cols, 2, lambdaStart, lambdaEnd, d_nnz[localIdx], o_nnz[localIdx]);
				// ET portion
				for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
				{
					for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
					{
						if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h && fabs(zCoord-z[*l]) < 1.5*h)
						{
							col = globalIndexMapping[*l] + 2*numBoundaryPointsOnProcess[procIdx];
							(col>=lambdaStart && col<lambdaEnd)? d_nnz[localIdx]++ : o_nnz[localIdx]++;
						}
					}
				}
				localIdx++;
			}
		}
	}
	
	// allocate memory for the matrix
	ierr = MatCreate(PETSC_COMM_WORLD, &BNQ); CHKERRQ(ierr);
	ierr = MatSetType(BNQ, MATMPIAIJ); CHKERRQ(ierr);
	ierr = MatSetSizes(BNQ, qLocalSize, lambdaLocalSize, PETSC_DETERMINE, PETSC_DETERMINE); CHKERRQ(ierr);
	ierr = MatMPIAIJSetPreallocation(BNQ, 0, d_nnz, 0, o_nnz); CHKERRQ(ierr);

	// deallocate d_nnz and o_nnz
	ierr = PetscFree(d_nnz); CHKERRQ(ierr);
	ierr = PetscFree(o_nnz); CHKERRQ(ierr);

	// assemble matrix Q
	localIdx = 0;
	// U
	ierr = DMDAGetCorners(uda, &mstart, &nstart, &pstart, &m, &n, &p); CHKERRQ(ierr);
	for(PetscInt k=pstart; k<pstart+p; k++)
	{
		zCoord = 0.5*(mesh->z[k] + mesh->z[k+1]);
		for(PetscInt j=nstart; j<nstart+n; j++)
		{
			yCoord = 0.5*(mesh->y[j] + mesh->y[j+1]);
			for(PetscInt i=mstart; i<mstart+m; i++)
			{
				h = mesh->dx[i];
				xCoord = mesh->x[i+1];
				row = localIdx + qStart;
				// G portion
				cols[0] = pGlobalIdx[k][j][i];
				cols[1] = pGlobalIdx[k][j][i+1];
				ierr = MatSetValues(BNQ, 1, &row, 2, cols, values, INSERT_VALUES); CHKERRQ(ierr);
				// ET portion
				for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
				{
					for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
					{
						if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h && fabs(zCoord-z[*l]) < 1.5*h)
						{
							col  = globalIndexMapping[*l];
							value= h*delta(xCoord-x[*l], yCoord-y[*l], zCoord-z[*l], h);
							ierr = MatSetValue(BNQ, row, col, value, INSERT_VALUES); CHKERRQ(ierr);
						}
					}
				}
				localIdx++;
			}
		}
	}
	// V
	ierr = DMDAGetCorners(vda, &mstart, &nstart, &pstart, &m, &n, &p); CHKERRQ(ierr);
	for(PetscInt k=pstart; k<pstart+p; k++)
	{
		zCoord = 0.5*(mesh->z[k] + mesh->z[k+1]);
		for(PetscInt j=nstart; j<nstart+n; j++)
		{
			h = mesh->dy[j];
			yCoord = mesh->y[j+1];
			for(PetscInt i=mstart; i<mstart+m; i++)
			{
				xCoord = 0.5*(mesh->x[i] + mesh->x[i+1]);
				row = localIdx + qStart;
				// G portion
				cols[0] = pGlobalIdx[k][j][i];
				cols[1] = pGlobalIdx[k][j+1][i];
				ierr = MatSetValues(BNQ, 1, &row, 2, cols, values, INSERT_VALUES); CHKERRQ(ierr);
				// ET portion
				for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
				{
					for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
					{
						if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h && fabs(zCoord-z[*l]) < 1.5*h)
						{
							col  = globalIndexMapping[*l] + numBoundaryPointsOnProcess[procIdx];
							value= h*delta(xCoord-x[*l], yCoord-y[*l], zCoord-z[*l], h);
							ierr = MatSetValue(BNQ, row, col, value, INSERT_VALUES); CHKERRQ(ierr);
						}
					}
				}
				localIdx++;
			}
		}
	}
	// W
	ierr = DMDAGetCorners(wda, &mstart, &nstart, &pstart, &m, &n, &p); CHKERRQ(ierr);
	for(PetscInt k=pstart; k<pstart+p; k++)
	{
		h = mesh->dz[k];
		zCoord = mesh->z[k+1];
		for(PetscInt j=nstart; j<nstart+n; j++)
		{
			yCoord = 0.5*(mesh->y[j] + mesh->y[j+1]);
			for(PetscInt i=mstart; i<mstart+m; i++)
			{
				xCoord = 0.5*(mesh->x[i] + mesh->x[i+1]);
				row = localIdx + qStart;
				// G portion
				cols[0] = pGlobalIdx[k][j][i];
				cols[1] = pGlobalIdx[k+1][j][i];
				ierr = MatSetValues(BNQ, 1, &row, 2, cols, values, INSERT_VALUES); CHKERRQ(ierr);
				// ET portion
				for(PetscInt procIdx=0; procIdx<numProcs; procIdx++)
				{
					for(auto l=boundaryPointIndices[procIdx].begin(); l!=boundaryPointIndices[procIdx].end(); l++)
					{
						if(fabs(xCoord-x[*l]) < 1.5*h  && fabs(yCoord-y[*l]) < 1.5*h && fabs(zCoord-z[*l]) < 1.5*h)
						{
							col  = globalIndexMapping[*l] + 2*numBoundaryPointsOnProcess[procIdx];
							value= h*delta(xCoord-x[*l], yCoord-y[*l], zCoord-z[*l], h);
							ierr = MatSetValue(BNQ, row, col, value, INSERT_VALUES); CHKERRQ(ierr);
						}
					}
				}
				localIdx++;
			}
		}
	}
	ierr = DMDAVecRestoreArray(pda, pMapping, &pGlobalIdx); CHKERRQ(ierr);

	ierr = MatAssemblyBegin(BNQ, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
	ierr = MatAssemblyEnd(BNQ, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);

	ierr = MatTranspose(BNQ, MAT_INITIAL_MATRIX, &QT); CHKERRQ(ierr);
	ierr = MatDiagonalScale(BNQ, BN, NULL); CHKERRQ(ierr);

	return 0;
}